/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// AudioPlayer.cpp
// 单任务架构(与USBTFCard参考工程同构): 命令入队, 音频任务串行处理, 无跨任务死锁
#include "AudioPlayer.h"
#include "../Bsp/IOPin.h"
#include "../Services/SystemSetting.h"
#include "../Bsp/I2CBus.h"
#include <math.h>

// 全局系统设置实例（定义于主入口.ino）
extern SystemSetting systemSetting;

#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "driver/gpio.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "AudioFileSourceFS.h"
#include "AudioGenerator.h"
#include "AudioGeneratorMP3.h"
#include "AudioGeneratorWAV.h"
#include "AudioGeneratorFLAC.h"
#include "AudioGeneratorAAC.h"
#include "AudioGeneratorOpus.h"
#include "AudioGeneratorMIDI.h"
#include "AudioGeneratorMOD.h"
#include "../Services/TalkManager.h"
#include "../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AUDIO"

AudioPlayer audioPlayer;

extern SystemSetting systemSetting;

const char* AudioPlayer::MOUNT_POINT = "/sdcard";
static const int AUDIO_SAMPLE_RATE = 44100;

// I2S写超时: DMA满时write快速失败(不长期阻塞), 保证命令处理点可达
static const uint32_t I2S_WRITE_TIMEOUT_MS = 100;

// 解码器错误回调(诊断)
static void decoderStatusCB(void *cbData, int code, const char *str) {
    (void)cbData;
    LOG_E("AP[错误] 解码器 (%d) %s", code, str ? str : "");
}

// ================= 输出适配器(仅音频任务访问) =================
AudioOutputWM8960::AudioOutputWM8960(I2SClass *i2s)
    : _i2s(i2s), _count(0), _rampStart(0), _baseMs(0), _samples(0), _curRate(0) {
    hertz = 0;
    SetGain(0.75);  // 数字增益0.75(-2.5dB), 削峰限流(参考工程验证值)
}

bool AudioOutputWM8960::SetRate(int hz) {
    if (_curRate > 0 && _samples > 0) {
        _baseMs += (uint32_t)((uint64_t)_samples * 1000ULL / _curRate);
        _samples = 0;
    }
    _curRate = (hz > 0) ? hz : 0;
    hertz = hz;
    if (hz > 0 && (uint32_t)hz != _i2s->txSampleRate()) {
        LOG_D("AP[SetRate] %u->%uHz 重配", (unsigned)_i2s->txSampleRate(), (unsigned)hz);
        unsigned long t0 = millis();
        flushBuf();
        _i2s->configureTX(hz, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
        if (millis() - t0 > 100) {
            LOG_D("AP[SetRate] 重配耗时%ums 异常!", (unsigned)(millis() - t0));
        }
    } else {
        LOG_D("AP[SetRate] %uHz 已就位(无需重配)", (unsigned)hz);
    }
    return true;
}

bool AudioOutputWM8960::begin() {
    _count = 0;
    _rampStart = millis();
    _baseMs = 0;      // 新曲目: 重置已播时长计数
    _samples = 0;
    return true;
}

bool AudioOutputWM8960::ConsumeSample(int16_t sample[2]) {
    // 协作让出(FLAC): 本批已写满并置位让出标志, 本次调用返回false不消费样本,
    // FLAC的do-while退出->loop()返回->主循环处理命令/快照。库会保留样本下次重发, 无损。
    if (yieldEnabled && yieldPending) {
        yieldPending = false;
        return false;
    }
    // 起播1秒内数字增益从0渐升(软启动), 避免瞬间大电流拉垮电源
    unsigned long el = millis() - _rampStart;
    int16_t lv, rv;
    if (el >= 1000) {
        lv = Amplify(sample[LEFTCHANNEL]);
        rv = Amplify(sample[RIGHTCHANNEL]);
    } else {
        float ramp = (float)el / 1000.0f;
        lv = (int16_t)(Amplify(sample[LEFTCHANNEL]) * ramp);
        rv = (int16_t)(Amplify(sample[RIGHTCHANNEL]) * ramp);
    }
    _buf[_count][0] = lv;
    _buf[_count][1] = rv;
    // VU窗口: 累积L/R最大幅值(供UI电平表, 50ms窗口由音频任务读取清零)
    uint16_t al = (lv < 0) ? (uint16_t)(-lv) : (uint16_t)lv;
    uint16_t ar = (rv < 0) ? (uint16_t)(-rv) : (uint16_t)rv;
    if (al > _vuMaxL) _vuMaxL = al;
    if (ar > _vuMaxR) _vuMaxR = ar;
    _count++;
    _samples++;
    pushWaveSample(lv, rv);
    if (_count >= BUF_SAMPLES) {
        flushBuf();
        if (yieldEnabled) yieldPending = true;   // 本批写完, 下次调用让出
    }
    return true;
}

void AudioOutputWM8960::pushWaveSample(int16_t l, int16_t r) {
    _ringL[_ringHead] = l;
    _ringR[_ringHead] = r;
    _ringHead = (_ringHead + 1) % RING_FRAMES;
    if (_ringCount < RING_FRAMES) _ringCount++;
}

bool AudioOutputWM8960::copyMono(int16_t* out, size_t len) const {
    if (!out || len == 0 || len > RING_FRAMES) return false;
    if (_ringCount < len) {
        // 数据不足: 前面补零
        size_t pad = len - _ringCount;
        for (size_t i = 0; i < pad; i++) out[i] = 0;
        out += pad;
        len = _ringCount;
        if (len == 0) return true;
    }
    uint16_t head = _ringHead;
    uint16_t start = (head + RING_FRAMES - (uint16_t)len) % RING_FRAMES;
    for (size_t i = 0; i < len; i++) {
        uint16_t idx = (start + i) % RING_FRAMES;
        int32_t mono = ((int32_t)_ringL[idx] + (int32_t)_ringR[idx]) / 2;
        out[i] = (int16_t)mono;
    }
    return true;
}

bool AudioOutputWM8960::copyWave(int16_t* outL, int16_t* outR, size_t len) const {
    if ((!outL && !outR) || len == 0 || len > RING_FRAMES) return false;
    if (_ringCount < len) {
        size_t pad = len - _ringCount;
        if (outL) for (size_t i = 0; i < pad; i++) outL[i] = 0;
        if (outR) for (size_t i = 0; i < pad; i++) outR[i] = 0;
        if (outL) outL += pad;
        if (outR) outR += pad;
        len = _ringCount;
        if (len == 0) return true;
    }
    uint16_t head = _ringHead;
    uint16_t start = (head + RING_FRAMES - (uint16_t)len) % RING_FRAMES;
    for (size_t i = 0; i < len; i++) {
        uint16_t idx = (start + i) % RING_FRAMES;
        if (outL) outL[i] = _ringL[idx];
        if (outR) outR[i] = _ringR[idx];
    }
    return true;
}

bool AudioOutputWM8960::stop() {
    flushBuf();
    return true;
}

uint32_t AudioOutputWM8960::getElapsedMs() const {
    uint32_t ms = _baseMs;
    if (_curRate > 0 && _samples > 0) {
        ms += (uint32_t)((uint64_t)_samples * 1000ULL / _curRate);
    }
    return ms;
}

// 写失败计数(探针): DMA停摆时write超时部分写入
volatile uint32_t apWriteFailCount = 0;

void AudioOutputWM8960::flushBuf() {
    // 注意: write部分返回时绝不能丢弃未写样本(会造成周期性断音/颤音),
    // 保留在_buf头部下次重发; 正常情况下DMA持续排空, write一次写满。
    while (_count > 0) {
        size_t want = _count * 4;
        unsigned long t0 = millis();
        size_t wrote = _i2s->write((uint8_t *)_buf, want);
        unsigned long el = millis() - t0;
        if (wrote < want) {
            apWriteFailCount++;
            size_t doneSamples = (wrote / 4);   // 按帧对齐(立体声帧=4字节)
            size_t leftSamples = _count - doneSamples;
            memmove(_buf, _buf + doneSamples, leftSamples * 4);
            _count = (int)leftSamples;
            vTaskDelay(1);   // 等DMA腾空间后继续, 不丢数据
        } else {
            _count = 0;
        }
        if (el > 200) {
            LOG_D("AP[flush] 写%u字节耗时%ums(DMA满?)",
                  (unsigned)want, (unsigned)el);
        }
    }
}

// ================= AudioPlayer =================
AudioPlayer::AudioPlayer() {
    // I2C总线锁归属I2CBus(总线对象), 本成员仅持引用供内部take/give;
    // 构造期调用begin()幂等创建, 与OLED侧(GUIRender)共享同一把锁
    i2cMutex = I2CBus::begin();
}

AudioPlayer::~AudioPlayer() {
    if (taskHandle) vTaskDelete(taskHandle);
    // 录音兜底收尾(任务已停, 无并发): 回写WAV头+关文件+关路径+还缓冲
    if (recFileOpen) finishRecFile();
    closeRecordPath();
    if (recBuf) {
        free(recBuf);
        recBuf = nullptr;
    }
    stopDecoder();
    closeAudioPath();
    if (output) { delete output; output = nullptr; }
    // 注: i2cMutex引用的是I2CBus共享锁, 归属总线对象, 此处不释放
}

bool AudioPlayer::mountSD() {
    if (cardReady) return true;
    if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0)) {
        LOG_E("AP[TF] 引脚配置失败");
        return false;
    }
    if (!SD_MMC.begin(MOUNT_POINT, true, false, 10000000)) {   // 1位模式, 10MHz
        LOG_E("AP[TF] 挂载失败(未插卡?)");
        return false;
    }
    LOG_I("AP[TF] 就绪, 容量 %llu MB", SD_MMC.cardSize() / 1024 / 1024);
    return true;
}

AudioInitState AudioPlayer::init() {
    if (initState == AUDIO_INIT_OK) return initState;

    // TF卡写入瞬间的us级电流冲击会误触发BOD复位, 关闭复位动作保留检测
    CLEAR_PERI_REG_MASK(RTC_CNTL_BROWN_OUT_REG, RTC_CNTL_BROWN_OUT_RST_ENA_M);

    // 0. 命令队列+音频任务: 最先创建且无条件执行。
    //    提示音(原蜂鸣器替代)只依赖WM8960+I2S, 不依赖TF卡;
    //    若放在TF卡挂载之后, 未插卡时队列永远建不起来, 滴声命令全部入队失败。
    if (!taskStarted) {
        cmdQueue = xQueueCreate(8, sizeof(AudioCmdMsg));
        xTaskCreatePinnedToCore(audioTaskFunc, "AudioTask", TASK_STACK, this, 3, &taskHandle, 0);
        taskStarted = true;
        LOG_I("AP[任务] 音频任务已启动(Core0)");
    }

    // 1. TF卡(仅音乐播放需要; 失败不影响提示音)
    if (!cardReady) {
        cardReady = mountSD();
        if (!cardReady) {
            initState = AUDIO_INIT_NO_CARD;
            return initState;
        }
    }

    // 2. WM8960在线探测: 仅I2C地址ACK(与OLED共享总线, 持锁)
    if (!codecChecked) {
        xSemaphoreTake(i2cMutex, portMAX_DELAY);
        Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
        Wire.beginTransmission(WM8960_ADDR);
        codecPresent = (Wire.endTransmission() == 0);
        xSemaphoreGive(i2cMutex);
        codecChecked = true;
        LOG_I("AP[WM8960] %s", codecPresent ? "在线" : "无响应");
        if (!codecPresent) {
            initState = AUDIO_INIT_NO_CODEC;
            return initState;
        }
    }

    // 3. 全部就绪
    initState = AUDIO_INIT_OK;
    return initState;
}

// ================= 音频路径(仅音频任务; I2C段持锁) =================
bool AudioPlayer::openAudioPath() {
    if (audioPathOpen) return true;

    i2s.setPins(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_DIN, PIN_I2S_MCLK);
    if (!i2s.begin(I2S_MODE_STD, AUDIO_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
        LOG_E("AP[路径] I2S初始化失败");
        return false;
    }
    i2s.setTimeout(I2S_WRITE_TIMEOUT_MS);
    // DACLRC与ADCLRC需同频同相: GPIO矩阵把I2S0的WS复制到ADCLRC
    gpio_set_direction((gpio_num_t)PIN_I2S_ALRC, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(PIN_I2S_ALRC, I2S0O_WS_OUT_IDX, false, false);

    {
        // WM8960防爆音上电时序(依据datasheet v4.2 Table26/27与Linux wm8960.c
        // set_bias_level_capless())。持锁段只写寄存器, 所有等待delay在锁外,
        // 避免长时间占I2C总线导致OLED渲染卡顿。
        codecI2cLock();
        bool ok = codec.begin(Wire);
        if (ok) {
            // 段1: 数字静音+模拟音量0+零交叉+R28防爆音位, 再起VREF
            codec.antiPopPowerUpBegin();
            codec.enableVREF();
            codec.setVMID(WM8960_VMIDSEL_2X250KOHM);  // 慢充, 减小VMID跳变台阶
        }
        codecI2cUnlock();
        if (!ok) {
            LOG_E("AP[路径] WM8960无响应");
            i2s.end();
            return false;
        }

        delay(700);   // VMID电容慢充电(至少500ms, 锁外等待不挡OLED)

        codecI2cLock();
        codec.enablePeripheralMode();               // WM8960作I2S从机
        codec.setCLKSEL(WM8960_CLKSEL_MCLK);        // SYSCLK=MCLK=256fs直通
        codec.setSYSCLKDIV(WM8960_SYSCLK_DIV_BY_1);
        codec.setADCDIV(0);
        codec.setDACDIV(0);
        codec.setDCLKDIV(WM8960_DCLKDIV_16);
        codec.setWL(WM8960_WL_16BIT);

        codec.enableDacLeft();
        codec.enableDacRight();
        codec.enableLD2LO();   // 左DAC -> 左输出混音
        codec.enableRD2RO();
        codec.enableLOMIX();
        codec.enableROMIX();

        codec.enableOUT3asVMID();   // OUT3输出缓冲VMID作capless耳机地
        codec.enableOUT3MIX();

        // ---- 输出路由: 耳机(OUT1) + 喇叭(OUT2/SPKOUT Class-D) 同时开启 ----
        // 插耳机: 耳机出声(喇叭同时出声, 芯片无自动切换能力);
        // 拔耳机: 耳机座物理断开, 声音自然只从喇叭出 -> 拔插不断播。
        // 此时仍在HPSTBY中, 功放上电无冲击。
        codec.enableHeadphones();
        if (speakerOn) {
            codec.enableSpeakers();   // PWR2[4:3]+ClassD[7:6], 音量在applyVolumeLocked里写
        }
        codecI2cUnlock();

        delay(100);   // 输出级稳定(锁外)

        codecI2cLock();
        codec.setVMID(WM8960_VMIDSEL_2X50KOHM);   // 先切正常阻抗
        codecI2cUnlock();

        delay(50);    // VMID切档后稳定(锁外)

        codecI2cLock();
        codec.antiPopPowerUpRelease();            // 最后释HPSTBY/清SOFT_ST(单事件)

        // ---- 耳机插入检测(HPSWEN): 启用(本板已按规格书设计: LINPUT3经33K上拉AVDD +
        //      耳机座shunt检测开关, 未插入=接地, 插入=断开->拉高) ----
        // 芯片自治行为(规格书p45 Table29): 插入 -> 自动打开耳机驱动(带防爆破音时序);
        // 拔出 -> 自动关闭耳机驱动(省电, 开路不输出)。注意: 它只管理耳机口自己,
        // 永远不会开/关喇叭; 且WM8960寄存器只写不读, 检测结果也通知不到ESP32,
        // 所以"插耳机时自动静音喇叭"必须把检测脚另接一颗ESP32 GPIO(本板未接)。
        // 当前策略: 喇叭常开, 拔插耳机均不断播。
        // 若实测"耳机完全无声而喇叭正常" => 检测电路未生效,
        // 注释掉下面两行即回到"耳机常开"模式。
        codec.enableHeadphoneJackDetect();
        codec.setHeadphoneJackDetectInput(WM8960_JACKDETECT_LINPUT3);

        // DAC数字音量antiPopBegin已置0dB并全程保持; 此处只ramp模拟音量
        // (DAC仍mute, 无声切换)。起播时prime静音帧后直接解mute即可, 无第二次跳变。
        rampVolumeForTargetLocked();
        // 保持DAC静音, tryStart起播前先prime静音帧再解除
        codecI2cUnlock();
    }

    if (!output) {
        output = new AudioOutputWM8960(&i2s);
    }

    audioPathOpen = true;
    LOG_I("AP[路径] 音频路径已开启");
    return true;
}

void AudioPlayer::closeAudioPath() {
    if (!audioPathOpen) return;
    // 软关断(防爆音): 先数字静音+模拟音量归零, 待输出稳定后再逐级断电。
    // 禁用codec.reset()硬复位: 它瞬间拉掉VMID, 下次open必"嘭"一声。
    codecI2cLock();
    codec.enableDacMute();
    codec.setHeadphoneVolume(0);
    codec.setSpeakerVolume(0);
    codecI2cUnlock();
    delay(30);
    codecI2cLock();
    codec.disableHeadphones();
    codec.disableSpeakers();
    codec.disableLOMIX();
    codec.disableROMIX();
    codec.disableOUT3MIX();
    codec.disableDacLeft();
    codec.disableDacRight();
    codec.setVMID(WM8960_VMIDSEL_2X250KOHM);
    codecI2cUnlock();
    delay(50);
    codecI2cLock();
    codec.disableVMID();
    codec.disableVREF();
    codecI2cUnlock();
    i2s.end();       // 停DMA与时钟, 声音必停
    audioPathOpen = false;
    LOG_I("AP[路径] 音频路径已关闭");
}

// 起播前预送的静音帧数(约23ms@44.1k): 让I2S DMA先跑起来, DAC输出稳定后再解mute,
// 避免DAC mute释放瞬间的残留数据/时钟毛刺变成"嗒"声
static const int AP_PRIME_SILENT_FRAMES = 1024;

// ================= WM8960 I2C访问(与OLED共享总线, 仅互斥不降速) =================
void AudioPlayer::codecI2cLock() {
    xSemaphoreTake(i2cMutex, portMAX_DELAY);
}

void AudioPlayer::codecI2cUnlock() {
    xSemaphoreGive(i2cMutex);
}

// 喇叭dB: 音量曲线同耳机(1..100 -> -73..+6dB)但封顶0dB —— Class-D满幅输出电流大,
// 锂电池直供有欠压风险(参考工程因功耗禁用了喇叭); 需要更大音量可放宽上限
float AudioPlayer::spkVolumeDb(int vol) {
    if (vol <= 0) return -74.0f;   // 真静音档
    float db = -73.0f + ((vol - 1) * 79.0f / 99.0f);
    return (db > 0.0f) ? 0.0f : db;
}

bool AudioPlayer::applyVolumeLocked() {
    // 已持i2cMutex: 同时写耳机(LOUT1/ROUT1)与喇叭(LOUT2/ROUT2)模拟音量; 返回I2C是否ACK
    // 耳机: 1..100 线性映射到 -73..+6 dB (默认50约-34dB); 喇叭封顶0dB(spkVolumeDb)
    // HP/SPK均已开零交叉, 正常播放中调音量用此一步到位即可
    int vol = targetVolume;
    float hpDb = (vol <= 0) ? -74.0f : -73.0f + ((vol - 1) * 79.0f / 99.0f);
    bool ok = codec.setHeadphoneVolumeDB(hpDb) &&
              codec.setSpeakerVolumeDB(spkVolumeDb(vol));
    LOG_I("AP[音量] 写芯片 vol=%d (耳机/喇叭) -> %s",
          (int)targetVolume, ok ? "OK" : "失败(NACK!)");
    return ok;
}

bool AudioPlayer::rampVolumeForTargetLocked() {
    // 已持i2cMutex: 上电/开路时模拟音量从mute软斜坡到目标(替代applyVolumeLocked
    // 的一步到位, 消除"0->目标"的跳变啪声)。播放中调音量仍走applyVolumeLocked。
    int vol = targetVolume;
    float hpDb = (vol <= 0) ? -74.0f : -73.0f + ((vol - 1) * 79.0f / 99.0f);
    bool ok = codec.rampVolumeToTarget(hpDb, spkVolumeDb(vol));
    LOG_I("AP[音量] 斜坡 vol=%d (耳机/喇叭) -> %s",
          (int)targetVolume, ok ? "OK" : "失败(NACK!)");
    return ok;
}

void AudioPlayer::primeSilentFrames() {
    // 仅音频任务: 起播前向I2S预送静音帧, 让DMA/WS时钟先跑稳。
    // 调用时DAC处于mute态, 预送数据不会出声, 只消除mute释放瞬间的时钟毛刺。
    static int16_t silence[256][2];   // 零初始化, 常驻约1KB
    memset(silence, 0, sizeof(silence));
    int left = AP_PRIME_SILENT_FRAMES;
    while (left > 0) {
        int cnt = (left > 256) ? 256 : left;
        size_t want = (size_t)(cnt * sizeof(int16_t) * 2);
        if (i2s.write((uint8_t*)silence, want) != want) break;
        left -= cnt;
    }
}

// ================= 音频任务(唯一: 命令处理+解码) =================
void AudioPlayer::audioTaskFunc(void* param) {
    AudioPlayer* self = (AudioPlayer*)param;
    self->audioTaskLoop();
}

void AudioPlayer::audioTaskLoop() {
    AudioCmdMsg msg;
    unsigned long lastAliveMs = 0;
    uint32_t lastElapsed = 0;
    // 探针: loop调用计数/最大耗时/心跳
    uint32_t loopCount = 0;
    uint32_t loopMaxMs = 0;
    unsigned long lastBeatMs = 0;
    while (true) {
        // ---- 心跳探针(播放/暂停/录音/对讲/启动中每3秒; 空闲降到60秒保活证明) ----
        uint32_t beatPeriod = (state == PLAYER_IDLE && recStateV == REC_IDLE &&
                               talkStateV == TALK_IDLE) ? 60000 : 3000;
        if (millis() - lastBeatMs > beatPeriod) {
            lastBeatMs = millis();
            LOG_D("AP[心跳] state=%d rec=%d talk=%d idx=%d elapsed=%ums loop次数=%u loopMax=%ums 栈余=%u heap=%u 写失败=%u",
                  (int)state, (int)recStateV, (int)talkStateV, curIndex, (unsigned)snapElapsed,
                  (unsigned)loopCount, (unsigned)loopMaxMs,
                  (unsigned)uxTaskGetStackHighWaterMark(NULL),
                  (unsigned)ESP.getFreeHeap(), (unsigned)apWriteFailCount);
            loopMaxMs = 0;
        }

        // ---- 命令轮询(播放/录音/对讲中每2ms一查, 空闲时阻塞50ms省电) ----
        while (xQueueReceive(cmdQueue, &msg,
               (state == PLAYER_PLAYING || recStateV != REC_IDLE ||
                talkStateV != TALK_IDLE) ? 0 : pdMS_TO_TICKS(50)) == pdTRUE) {
            handleCommand(msg);
        }

        // ---- 录音模式: 引擎接管(内部自轮询命令/非阻塞读DMA/VU/落盘), 退出即已收尾 ----
        if (recStateV != REC_IDLE) {
            recordLoop();
            continue;
        }

        // ---- 对讲模式: 引擎接管(命令/双工DMA/ADPCM/ESP-NOW/VU), 退出即收尾 ----
        if (talkStateV != TALK_IDLE) {
            talkLoop();
            continue;
        }

        // ---- VU电平采样(50ms窗口, 播放/暂停/停止均衰减到0; 录音期间由recordLoop更新) ----
        if (millis() - lastVuAt >= 50) {
            lastVuAt = millis();
            uint16_t vl = 0, vr = 0;
            if (output) output->takeVU(vl, vr);
            int l = vuToPct(vl), r = vuToPct(vr);
            snapVuL = (uint8_t)l;
            snapVuR = (uint8_t)r;
            snapPeakL = (l >= snapPeakL) ? (uint8_t)l : (uint8_t)(snapPeakL > 2 ? snapPeakL - 2 : 0);
            snapPeakR = (r >= snapPeakR) ? (uint8_t)r : (uint8_t)(snapPeakR > 2 ? snapPeakR - 2 : 0);
        }

        // ---- 解码推进 ----
        if (state == PLAYER_PLAYING) {
            if (decoder && decoder->isRunning()) {
                unsigned long lt0 = millis();
                bool alive = decoder->loop();
                uint32_t lt = millis() - lt0;
                if (lt > loopMaxMs) loopMaxMs = lt;
                if (lt > 300) {
                    LOG_D("AP[loop] 单次耗时%ums(异常)", (unsigned)lt);
                }
                loopCount++;
                if (!alive) {
                    // 曲目播完(或异常终止): 按循环模式前进
                    LOG_I("AP[结束] idx=%d 已播%ums(模式%d)",
                          curIndex, (unsigned)snapElapsed, playModeV);
                    advanceOnEnded();
                    lastAliveMs = millis();
                    lastElapsed = snapElapsed;
                } else {
                    updateSnapshot();
                    // 自愈: loop返回但时长长时间不涨(内部空转), 10秒重启本曲
                    if (snapElapsed != lastElapsed) {
                        lastElapsed = snapElapsed;
                        lastAliveMs = millis();
                    } else if (lastAliveMs == 0) {
                        lastAliveMs = millis();
                    } else if (millis() - lastAliveMs > 10000) {
                        LOG_W("AP[卡死] 时长10s未动(elapsed=%u), 重启本曲",
                              (unsigned)snapElapsed);
                        tryStart(curIndex);
                        lastAliveMs = millis();
                        lastElapsed = snapElapsed;
                    }
                }
            } else {
                advanceOnEnded();
            }
            // 不做固定延时: 每批仅128样本(2.9ms音频), DMA满时flushBuf的write会
            // 阻塞等待(任务自动让出, 不耗CPU)。固定delay会在GUI/SD抖动时让33ms DMA
            // 缓冲见底(auto_clear插零)→周期性颤音; taskYIELD仅做同优先级让路, 不延时。
            taskYIELD();
        } else {
            lastAliveMs = 0;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void AudioPlayer::handleCommand(const AudioCmdMsg& msg) {
    switch (msg.cmd) {
        case CMD_VOL:
            // 写WM8960耳机模拟音量(LOUT1/ROUT1, 与demo同款)
            // 对讲路径同样生效(通话页1/3键调听筒音量): talkPathOpen时实时写芯片
            if (audioPathOpen || talkPathOpen) {
                codecI2cLock();
                applyVolumeLocked();
                codecI2cUnlock();
            }
            LOG_I("AP[音量] 应用%d%s", targetVolume, (audioPathOpen || talkPathOpen) ? "" : "(待起播)");
            break;

        case CMD_BEEP:
            // 声音提示(原蜂鸣器): 仅空闲时播放, 不打断音乐/录音/对讲
            if (state == PLAYER_IDLE && recStateV == REC_IDLE && talkStateV == TALK_IDLE) {
                playBeepTone(msg.index);
            } else {
                LOG_W("AP[滴声] 忽略(state=%d rec=%d talk=%d)",
                      (int)state, (int)recStateV, (int)talkStateV);
            }
            break;

        case CMD_PAUSE:
            if (state == PLAYER_PLAYING) {
                // 暂停只做数字静音, 不掉耳机/喇叭电: 反复上下电才是"嘭"声主因,
                // mute足以消除暂停期噪音, 恢复时也无上电冲击
                codecI2cLock();
                codec.enableDacMute();
                codecI2cUnlock();
                state = PLAYER_PAUSED;       // 解码loop停 → 时间冻结
                LOG_I("AP[暂停] 完成");
            } else {
                LOG_W("AP[暂停] 忽略(state=%d)", (int)state);
            }
            break;

        case CMD_RESUME:
            if (state == PLAYER_PAUSED) {
                // 与暂停对应: 只解静音, 输出级不断电故无冲击
                codecI2cLock();
                codec.disableDacMute();
                codecI2cUnlock();
                state = PLAYER_PLAYING;
                LOG_I("AP[恢复] 完成");
            } else {
                LOG_W("AP[恢复] 忽略(state=%d)", (int)state);
            }
            break;

        case CMD_SPK:
            // 喇叭功放开关(音乐页按键3): 排查Class-D开关噪声耦合, 亦可作"仅耳机"使用偏好。
            // Class-D上下电有冲击: 开=音量归零再上电, 稳定后恢复; 关=音量归零稳定后断电。
            // 耳机(OUT1)不受影响; 音量寄存器位置保留语义, 重开后按目标值重写。
            speakerOn = !speakerOn;
            if (audioPathOpen) {
                codecI2cLock();
                codec.setSpeakerVolume(0);   // 先归零(零交叉生效)
                codecI2cUnlock();
                delay(30);
                codecI2cLock();
                if (speakerOn) {
                    codec.enableSpeakers();
                    codecI2cUnlock();
                    delay(100);              // 功放稳定(锁外)
                    codecI2cLock();
                    codec.setSpeakerVolumeDB(spkVolumeDb(targetVolume));
                } else {
                    codec.disableSpeakers();
                }
                codecI2cUnlock();
            }
            LOG_I("AP[喇叭] %s%s", speakerOn ? "开启" : "关闭",
                  audioPathOpen ? "" : "(待下次起播)");
            break;

        case CMD_STOP:
            fullStop();
            LOG_I("AP[停止] 完成(解码器已释放+断电)");
            break;

        case CMD_PLAY:
            if (recStateV != REC_IDLE) {
                // 防御: 录音中收到播放命令(页面流程不可达) → 请求停录音, 本次播放忽略
                LOG_W("AP[切歌] 录音中, 忽略播放命令");
                recStateV = REC_IDLE;
                break;
            }
            LOG_I("AP[切歌] 命令idx=%d", msg.index);
            startTrack(msg.index);
            break;

        case CMD_LOAD:
            if (recStateV != REC_IDLE) {
                LOG_W("AP[装载] 录音中, 忽略装载命令");
                recStateV = REC_IDLE;
                break;
            }
            LOG_I("AP[装载] %d首, 起播idx=%d",
                  msg.list ? (int)msg.list->size() : 0, msg.index);
            fullStop();   // 保证干净状态(路径关闭+列表可换)
            if (msg.list && !msg.list->empty()) {
                playlist = *msg.list;
                startFailed = false;
                curIndex = -1;
                startTrack(msg.index);
            }
            break;

        // ================= 录音命令(recordLoop内轮询处理; 与播放互斥) =================
        case CMD_REC_ARM:
            // 开录音路径进入监听(电平表活动, 不写文件)
            if (recStateV == REC_IDLE) {
                if (state != PLAYER_IDLE) fullStop();   // 防御: 播放中先停
                if (openRecordPath()) {
                    recStateV = REC_STANDBY;
                }
            }
            break;

        case CMD_REC_START:
            // 建WAV文件开始写入(未监听则自动开路径)
            if (recStateV == REC_IDLE) {
                if (state != PLAYER_IDLE) fullStop();
                if (!openRecordPath()) break;
                recStateV = REC_STANDBY;
            }
            if (recStateV == REC_STANDBY) {
                if (msg.path && beginRecFile(*msg.path)) {
                    recError = false;
                    recStateV = REC_RECORDING;
                } else {
                    // 建文件失败(未插卡/卡满/目录异常): 置错误并同步收尾(幂等: 若
                    // recordLoop已运行, 其退出收尾对这些调用有守卫, 无害)
                    LOG_E("AP[录音] 无法开始(建文件失败)");
                    recError = true;
                    recStateV = REC_IDLE;
                    closeRecordPath();
                    if (recBuf) {
                        free(recBuf);
                        recBuf = nullptr;
                    }
                }
            }
            break;

        case CMD_REC_PAUSE:
            // 暂停写入(继续监听, 电平表保持活动)
            if (recStateV == REC_RECORDING) {
                recStateV = REC_PAUSED;
                LOG_I("AP[录音] 暂停(继续监听)");
            }
            break;

        case CMD_REC_RESUME:
            if (recStateV == REC_PAUSED) {
                recMuteUntil = millis() + 150;   // 丢弃恢复瞬间DMA内残留旧数据
                recStateV = REC_RECORDING;
                LOG_I("AP[录音] 继续");
            }
            break;

        case CMD_REC_STOP:
            // recordLoop检测到IDLE后统一收尾(剩余落盘+WAV头回写+关路径)
            if (recStateV != REC_IDLE) {
                recStateV = REC_IDLE;
                LOG_I("AP[录音] 停止命令, 收尾中");
            }
            break;

        case CMD_REC_VOL:
            // 对讲路径同样生效(通话页2/8键调麦增益): applyRecGainLocked按talkModeV
            // 区分: TALK开路时=左麦增益/右路静音(与开路时一致); 录音时按recSourceV
            if (recPathOpen || talkPathOpen) {
                codecI2cLock();
                if (talkPathOpen && !recPathOpen) {
                    // TALK路径: 左路(内置麦)=用户增益, 右路(耳机麦)静音
                    codec.setAdcLeftDigitalVolumeDB((float)recGainDb);
                    codec.setAdcRightDigitalVolume(0);
                } else {
                    applyRecGainLocked();
                }
                codecI2cUnlock();
            }
            break;

        case CMD_REC_SRC:
            // 切换输入源: recSourceV已由setRecSource(UI任务)更新, 此处重写左右ADC
            // 音量(选中=增益/未选=静音); 监听态电平表即时反映新源(所见即所得)
            if (recPathOpen) {
                codecI2cLock();
                applyRecGainLocked();
                codecI2cUnlock();
            }
            break;

        // ================= 对讲命令(talkLoop内轮询处理; 与播放/录音互斥) =================
        case CMD_TALK_START:
            // 开TALK双工引擎: 播放中先停; 录音中则拒绝(页面流程不可达, 防御)
            if (recStateV != REC_IDLE) {
                LOG_W("AP[对讲] 录音中, 忽略启动");
                break;
            }
            if (talkStateV == TALK_IDLE) {
                if (state != PLAYER_IDLE) fullStop();
                talkPttV = false;
                talkMutedV = false;
                if (openTalkPath()) {
                    talkStateV = TALK_ACTIVE;
                }
            }
            break;

        case CMD_TALK_STOP:
            // talkLoop检测到IDLE后统一收尾(关路径回主循环)
            if (talkStateV != TALK_IDLE) {
                talkStateV = TALK_IDLE;
                LOG_I("AP[对讲] 停止命令, 收尾中");
            }
            break;

        case CMD_TALK_PTT:
            // PTT按下态(msg.index=0/1); talkLoop按模式与静音决定是否编码发送
            talkPttV = (msg.index != 0);
            if (talkPttV) {
                talkEncPred = 0; talkEncIdx = 0;   // 按下沿重置编码器(防续接爆音)
                talkRsAcc = 0; talkRsLast = 0;
            }
            break;

        case CMD_TALK_MUTE:
            talkMutedV = (msg.index != 0);
            LOG_I("AP[对讲] 静音%s", talkMutedV ? "开" : "关");
            break;

        case CMD_TALK_MODE:
            // 模式切换(msg.index=0 PTT/1全双工): 重置编解码与重采样累积
            talkModeV = (msg.index == 1) ? 1 : 0;
            talkEncPred = 0; talkEncIdx = 0;
            talkDecPred = 0; talkDecIdx = 0;
            talkRsAcc = 0; talkUsAcc = 0; talkUsPos = 0; talkUsLen = 0;
            LOG_I("AP[对讲] 模式→%s", talkModeV ? "全双工(8k)" : "PTT(16k)");
            break;

        default:
            break;
    }
}

void AudioPlayer::advanceOnEnded() {
    codecI2cLock();
    codec.enableDacMute();
    codecI2cUnlock();
    int size = (int)playlist.size();
    if (size == 0 || curIndex < 0) {
        fullStop();
        return;
    }
    bool ok = false;
    switch ((PlayMode)playModeV) {
        case PLAYMODE_REPEAT_ONE:
            ok = tryStart(curIndex);
            break;
        case PLAYMODE_RANDOM: {
            if (size > 1) {
                int r;
                do {
                    r = esp_random() % size;
                } while (r == curIndex);
                ok = tryStart(r);
            } else {
                ok = tryStart(0);
            }
            break;
        }
        case PLAYMODE_REPEAT_ALL:
            ok = tryStart((curIndex + 1) % size);
            break;
        case PLAYMODE_SEQUENCE:
        default:
            if (curIndex + 1 < size) {
                ok = tryStart(curIndex + 1);
            }
            break;
    }
    // 下一曲启动失败(文件损坏/TF卡拔出等): 关闭路径断电,
    // 否则WM8960保持使能+I2S空转, 无声却持续耗电
    if (!ok) fullStop();
}

// ================= 曲目启动(音频任务内, demo同款串行流程) =================
bool AudioPlayer::startTrack(int index) {
    int size = (int)playlist.size();
    if (size == 0) return false;
    if (index < 0 || index >= size) index = 0;

    // 尝试指定曲目, 失败逐个试后续(跳过坏文件), 全败置startFailed
    if (tryStart(index)) return true;
    for (int k = 1; k < size; k++) {
        if (tryStart((index + k) % size)) return true;
    }
    LOG_E("AP[启动] 播放列表全部失败");
    fullStop();
    startFailed = true;
    return false;
}

bool AudioPlayer::tryStart(int index) {
    LOG_I("AP[启动] idx=%d %s", index, playlist[index].c_str());
    unsigned long t0 = millis();
    state = PLAYER_STARTING;

    // 1. 停旧(demo官方方法: decoder->stop()安全, delete走析构, 无泄漏)
    if (audioPathOpen) {
        codecI2cLock();
        codec.enableDacMute();   // 切歌瞬间静音
        codecI2cUnlock();
    }
    stopDecoder();
    LOG_D("AP[启动] 停旧完成(%ums)", (unsigned)(millis() - t0));

    // 2. 路径未开则开(首次播放)
    if (!audioPathOpen) {
        if (!openAudioPath()) {
            LOG_E("AP[启动] 失败: 路径开启失败");
            state = PLAYER_IDLE;
            return false;
        }
    }

    String path = playlist[index];
    String lower = path;
    lower.toLowerCase();

    // 3. 头解析: 时长+采样率+位深
    uint32_t probedRate = 0;
    uint16_t probedBits = 0;
    uint32_t estMs = estimateTotalMs(path, lower, &probedRate, &probedBits);
    LOG_I("AP[启动] 头解析: 时长%ums 采样率%uHz 位深%ubit",
          (unsigned)estMs, (unsigned)probedRate, (unsigned)probedBits);

    // 4. 采样率预配置: 解码器SetRate发现时钟已就位则跳过configureTX
    //    (configureTX内部channel_disable在DMA循环播放旧数据时可能长时间不返回)
    if (probedRate > 0 && probedRate != i2s.txSampleRate()) {
        LOG_I("AP[启动] 预配I2S时钟 %uHz", (unsigned)probedRate);
        i2s.configureTX(probedRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    }

    // 5. 起新解码器(每曲新建: ESP8266Audio解码器begin不支持重入, demo同款stop+new)
    AudioGenerator* dec = createDecoder(lower);
    if (!dec) {
        LOG_E("AP[启动] 失败: 未知格式");
        state = PLAYER_IDLE;
        return false;
    }
    dec->RegisterStatusCB(decoderStatusCB, nullptr);
    // ESP8266Audio 2.x所有解码器(MP3/WAV/FLAC/AAC/Opus/MOD)的loop()都是do-while结构,
    // 靠ConsumeSample返回false才让出主循环(未消费样本下次重发, 无损)。
    // 若恒返回true, 解码器在一次loop()里解完整首歌(DMA背压阻塞), 期间暂停/停止/切歌
    // 命令全部得不到处理——MP3表现为"无法暂停/停止, 返回后还在放"。故全格式统一启用。
    output->setYieldEnabled(true);

    AudioFileSourceFS* file = new AudioFileSourceFS(SD_MMC, path.c_str());
    if (!dec->begin(file, output)) {
        // begin失败不能调stop()(ESP8266Audio的output未初始化缺陷), 直接delete走析构
        delete dec;
        delete file;
        state = PLAYER_IDLE;
        LOG_E("AP[启动] 失败: begin (heap %u/%u)",
              (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
        return false;
    }

    decoder = dec;
    audioFile = file;
    curIndex = index;
    startFailed = false;
    snapTotal = estMs;
    snapBits = (probedBits >= 8 && probedBits <= 32) ? probedBits : 16;  // 未解析到按16bit(PCM输出深度)
    snapSize = file->getSize();
    snapPos = 0;
    snapElapsed = 0;

    // 6. 起播: DAC数字音量全程0dB, 模拟音量开路时已ramp到目标,
    //    此处只prime静音帧(让DMA跑稳)后直接解mute, 单事件无台阶
    primeSilentFrames();
    codecI2cLock();
    codec.disableDacMute();
    codecI2cUnlock();

    state = PLAYER_PLAYING;
    updateSnapshot();
    LOG_I("AP[播放] %s (%u Hz, 耗时%ums)",
          path.c_str(), (unsigned)snapRate, (unsigned)(millis() - t0));
    return true;
}

void AudioPlayer::stopDecoder() {
    // demo官方停止方法: stop()通知解码器收尾, delete走析构(FLAC内部资源正常释放, 无泄漏)
    if (decoder) {
        if (decoder->isRunning()) decoder->stop();
        delete decoder;
        decoder = nullptr;
    }
    if (audioFile) {
        delete audioFile;
        audioFile = nullptr;
    }
}

void AudioPlayer::fullStop() {
    stopDecoder();
    closeAudioPath();
    state = PLAYER_IDLE;
    curIndex = -1;
    startFailed = false;
    snapElapsed = 0;
    snapTotal = 0;
    snapPos = 0;
    snapSize = 0;
    snapRate = 0;
    snapBits = 0;
    snapVuL = 0; snapVuR = 0;
    snapPeakL = 0; snapPeakR = 0;
}

void AudioPlayer::updateSnapshot() {
    snapElapsed = output ? output->getElapsedMs() : 0;
    snapPos = audioFile ? audioFile->getPos() : 0;
    snapRate = i2s.txSampleRate();
}

bool AudioPlayer::getSpectrumSamples(int16_t* out, size_t len) const {
    if (!output || !out || len == 0) return false;
    return output->copyMono(out, len);
}

bool AudioPlayer::getWaveformSamples(int16_t* outL, int16_t* outR, size_t len) const {
    if (!output) return false;
    return output->copyWave(outL, outR, len);
}

// ================= 命令接口(UI任务调用, 全部非阻塞入队) =================
bool AudioPlayer::loadPlaylist(const std::vector<String>& files, int startIndex) {
    if (files.empty()) return false;
    AudioCmdMsg msg = {};   // 零初始化(path等未用字段不带栈垃圾)
    msg.cmd = CMD_LOAD;
    msg.index = (startIndex >= 0 && startIndex < (int)files.size()) ? startIndex : 0;
    msg.list = &files;   // 音频任务同步拷贝(队列容量8, 处理在50ms内; 调用方files为页面成员, 存活期覆盖)
    return (xQueueSend(cmdQueue, &msg, 0) == pdTRUE);
}

// 命令入队(失败=队列满, 音频任务没在消费, 打印告警)
void AudioPlayer::postCmd(QueueHandle_t q, AudioCmdMsg& msg, const char* name) {
    if (!q) {
        // 队列未创建(init未执行): 开机流程已在setup()初始化, 此处仅防御
        LOG_E("AP[命令] 入队失败(音频任务未启动): %s", name);
        return;
    }
    if (xQueueSend(q, &msg, 0) != pdTRUE) {
        LOG_W("AP[命令] 入队失败(队列满): %s", name);
    }
}

void AudioPlayer::playIndex(int index) {
    if (playlist.empty()) return;
    if (index < 0 || index >= (int)playlist.size()) index = 0;
    startFailed = false;
    AudioCmdMsg msg = { CMD_PLAY, index, nullptr };
    postCmd(cmdQueue, msg, "PLAY");
}

void AudioPlayer::stop() {
    AudioCmdMsg msg = { CMD_STOP, 0, nullptr };
    postCmd(cmdQueue, msg, "STOP");
}

void AudioPlayer::pause() {
    if (state == PLAYER_PLAYING) {
        AudioCmdMsg msg = { CMD_PAUSE, 0, nullptr };
        postCmd(cmdQueue, msg, "PAUSE");
    }
}

void AudioPlayer::resume() {
    if (state == PLAYER_PAUSED) {
        AudioCmdMsg msg = { CMD_RESUME, 0, nullptr };
        postCmd(cmdQueue, msg, "RESUME");
    }
}

void AudioPlayer::setVolume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    targetVolume = vol;   // UI立即读到新值(百分比显示)

    // 直接调用会让“调音量”在队列短暂拥塞时更容易丢失；这里保留命令队列
    // 但如果当前已经在播放且队列写失败，则同步补写一次，确保芯片音量立即更新。
    AudioCmdMsg msg = { CMD_VOL, 0, nullptr };
    if (cmdQueue && xQueueSend(cmdQueue, &msg, 0) != pdTRUE) {
        LOG_W("AP[命令] 音量命令入队失败，立即重试");
        if (audioPathOpen) {
            codecI2cLock();
            applyVolumeLocked();
            codecI2cUnlock();
        }
    }
}

void AudioPlayer::setSpeakerOn(bool on) {
    // 入队切换(音频任务持有WM8960); 相同状态不重复入队
    if (on == speakerOn) return;
    AudioCmdMsg msg = { CMD_SPK, 0, nullptr };
    postCmd(cmdQueue, msg, "SPK");
}

// ================= 提示音(原蜂鸣器替代, WM8960喇叭播放) =================
bool AudioPlayer::beep(uint16_t durationMs) {
    // 播放/暂停中不打断音乐, 直接忽略
    if (state != PLAYER_IDLE) return false;
    if (durationMs == 0 || durationMs > 2000) durationMs = 100;

    AudioCmdMsg msg = { CMD_BEEP, (int)durationMs, nullptr };
    if (!cmdQueue) {
        // 队列未创建: init()尚未被调用(正常开机流程应在setup()中初始化音频模块)
        LOG_E("AP[命令] 滴声失败: 音频任务未初始化");
        return false;
    }
    if (xQueueSend(cmdQueue, &msg, 0) != pdTRUE) {
        LOG_W("AP[命令] 滴声命令入队失败(队列满)");
        return false;
    }
    return true;
}

void AudioPlayer::playBeepTone(int durationMs) {
    // 仅音频任务调用: 开音频路径 -> prime静音帧后解mute -> 正弦波直写I2S -> 静音 -> 软关断
    // 波形: 约2.7kHz正弦(模拟压电蜂鸣器的"滴"声), 首5ms淡入/末5ms淡出防爆音
    // DAC数字音量全程0dB, 模拟音量开路时已ramp到目标, 此处只解mute(单事件)
    bool pathWasOpen = audioPathOpen;
    if (!openAudioPath()) {
        LOG_E("AP[滴声] 音频路径开启失败");
        return;
    }

    primeSilentFrames();
    codecI2cLock();
    codec.disableDacMute();
    codecI2cUnlock();

    const float freq = 2700.0f;                       // 提示音频率
    const int totalSamples = (int)(AUDIO_SAMPLE_RATE * durationMs / 1000);
    const int fadeSamples = (int)(AUDIO_SAMPLE_RATE * 0.005f);  // 5ms淡入淡出
    const float amplitude = 12000.0f;                  // 峰值(满幅32767, 约-8.7dB)

    // 分块生成并阻塞写入(每块256样本立体声, write带100ms超时)
    const int CHUNK = 256;
    int16_t buf[CHUNK][2];
    for (int n = 0; n < totalSamples; ) {
        int cnt = 0;
        while (cnt < CHUNK && n < totalSamples) {
            float env = 1.0f;
            if (n < fadeSamples) env = (float)n / fadeSamples;                    // 淡入
            else if (n > totalSamples - fadeSamples) {
                env = (float)(totalSamples - n) / fadeSamples;                    // 淡出
            }
            int16_t s = (int16_t)(sinf(2.0f * (float)M_PI * freq * n / AUDIO_SAMPLE_RATE)
                                  * amplitude * env);
            buf[cnt][0] = s;
            buf[cnt][1] = s;
            cnt++; n++;
        }
        size_t want = (size_t)(cnt * sizeof(int16_t) * 2);
        if (i2s.write((uint8_t*)buf, want) != want) {
            LOG_W("AP[滴声] I2S写入超时, 提前结束");
            break;
        }
    }

    // 收尾: 尾音淡出后先静音, 路径是本次现开的才软关断(播音乐时借路, 不关)
    // DAC数字音量全程0dB不动, 只动mute(单事件, 无第二声跳变)
    codecI2cLock();
    codec.enableDacMute();
    codecI2cUnlock();
    if (!pathWasOpen) closeAudioPath();
    LOG_I("AP[滴声] %dms 完成", durationMs);
}

// 全局声音提示入口: 受"声音提示"系统设置控制(任意任务可调用, 非阻塞)
void SoundAlertBeep(uint16_t durationMs) {
    if (!systemSetting.getSoundAlertEnable()) return;   // 声音提示已关闭
    audioPlayer.beep(durationMs);
}

// ================= 录音引擎(仅音频任务; 配置与USBTFCard参考工程已验证通路一致) =================
// WAV头(44字节标准PCM头, 小端)
static void makeWavHeader(uint8_t* h, uint32_t dataSize, uint32_t sampleRate,
                          uint16_t channels, uint16_t bits) {
    uint32_t fmtSize = 16;
    uint32_t byteRate = sampleRate * channels * bits / 8;
    uint16_t blockAlign = channels * bits / 8;
    uint16_t fmt = 1;   // PCM
    uint32_t fileSize = 36 + dataSize;
    memcpy(h, "RIFF", 4);         memcpy(h + 4, &fileSize, 4);    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);    memcpy(h + 16, &fmtSize, 4);    memcpy(h + 20, &fmt, 2);
    memcpy(h + 22, &channels, 2); memcpy(h + 24, &sampleRate, 4); memcpy(h + 28, &byteRate, 4);
    memcpy(h + 32, &blockAlign, 2); memcpy(h + 34, &bits, 2);
    memcpy(h + 36, "data", 4);    memcpy(h + 40, &dataSize, 4);
}

bool AudioPlayer::openRecordPath() {
    if (recPathOpen) return true;
    if (audioPathOpen) closeAudioPath();   // 播放/录音互斥(共用I2S与WM8960)

    // I2S全双工: DOUT/DIN均已配置 → begin时TX/RX通道同时创建并使能(ESP_I2S库行为)
    i2s.setPins(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_DIN, PIN_I2S_MCLK);
    if (!i2s.begin(I2S_MODE_STD, AUDIO_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
        LOG_E("AP[录音] I2S初始化失败");
        return false;
    }
    i2s.setTimeout(I2S_WRITE_TIMEOUT_MS);
    // DACLRC与ADCLRC需同频同相: GPIO矩阵把WS复制到ADCLRC(与播放路径同款)
    gpio_set_direction((gpio_num_t)PIN_I2S_ALRC, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(PIN_I2S_ALRC, I2S0O_WS_OUT_IDX, false, false);

    {
        // WM8960录音通路(匹配本板硬件: LINPUT1=内置麦AOS3729A, RINPUT1=耳机麦, 均单端):
        // LINPUT1 -> 左PGA反相端(同相端接VMID, 单端标准接法) -> PGA -> Boost -> ADC -> I2S
        // RINPUT1 -> 右PGA反相端(同相端接VMID)            -> PGA -> Boost -> ADC -> I2S
        // 未选中源的通路由ADC数字音量0静音(见applyRecGainLocked), 悬空输入不进数据
        // 长delay在锁外, 避免占I2C总线挡OLED渲染
        codecI2cLock();
        bool ok = codec.begin(Wire);
        if (ok) {
            codec.enablePgaZeroCross();     // PGA零交叉, 增益切换无拉链音
            codec.enableVREF();
            codec.setVMID(WM8960_VMIDSEL_2X250KOHM);  // 慢充, 与播放路径同款防爆音
        }
        codecI2cUnlock();
        if (!ok) {
            LOG_E("AP[录音] WM8960无响应");
            i2s.end();
            return false;
        }

        delay(700);   // VMID慢充电(锁外)

        codecI2cLock();
        codec.enablePeripheralMode();               // WM8960作I2S从机
        codec.setCLKSEL(WM8960_CLKSEL_MCLK);        // SYSCLK=MCLK=256fs直通
        codec.setSYSCLKDIV(WM8960_SYSCLK_DIV_BY_1);
        codec.setADCDIV(0);
        codec.setDACDIV(0);
        codec.setWL(WM8960_WL_16BIT);
        codec.setAdcLeftDigitalVolume(0);           // 先静音, 输入链路稳定后再开(防上电瞬态)
        codec.setAdcRightDigitalVolume(0);

        codec.enableMicBias();                      // 驻极体麦克风偏置(内置麦+耳机麦)
        codec.setMicBiasVoltage(WM8960_MIC_BIAS_VOLTAGE_0_9_AVDD);
        codec.enableLMIC();
        codec.enableRMIC();
        codec.connectLMN1();                        // LINPUT1(内置麦) -> 左PGA反相端
        codec.connectRMN1();                        // RINPUT1(耳机麦) -> 右PGA反相端
        codec.pgaLeftNonInvSignalSelect(WM8960_PGAL_VMID);   // 左PGA同相端接VMID(单端接法)
        codec.pgaRightNonInvSignalSelect(WM8960_PGAR_VMID);  // 右PGA同相端接VMID
        codec.disableLINMUTE();
        codec.disableRINMUTE();
        codec.setLINVOLDB(20.0f);                   // 模拟增益(参考工程验证量级)
        codec.setRINVOLDB(20.0f);
        codec.setLMICBOOST(WM8960_MIC_BOOST_GAIN_13DB);   // PGA -> Boost的固定增益
        codec.setRMICBOOST(WM8960_MIC_BOOST_GAIN_13DB);
        codec.connectLMIC2B();                      // PGA -> Boost混音
        codec.connectRMIC2B();
        codec.enableAINL();                         // 输入缓冲
        codec.enableAINR();
        codec.enableAdcLeft();                      // ADC
        codec.enableAdcRight();
        codecI2cUnlock();

        delay(100);   // 输入链路稳定(锁外)

        codecI2cLock();
        applyRecGainLocked();                       // 解除静音 → 按源写增益
        codecI2cUnlock();
    }

    recPathOpen = true;
    recOverflow = 0;
    lastVuAt = 0;   // VU窗口立即起算
    LOG_I("AP[录音] 录音路径已开启(监听)");
    return true;
}

void AudioPlayer::closeRecordPath() {
    if (!recPathOpen) return;
    // 软关断: ADC先静音再逐级断电, 替代reset()硬复位(VMID瞬间塌掉, 耳机监听会有"嘭"声)
    codecI2cLock();
    codec.setAdcLeftDigitalVolume(0);
    codec.setAdcRightDigitalVolume(0);
    codec.disableAdcLeft();
    codec.disableAdcRight();
    codec.disableAINL();
    codec.disableAINR();
    codec.disableLMIC();
    codec.disableRMIC();
    codec.disableMicBias();
    codec.setVMID(WM8960_VMIDSEL_2X250KOHM);
    codecI2cUnlock();
    delay(50);
    codecI2cLock();
    codec.disableVMID();
    codec.disableVREF();
    codecI2cUnlock();
    i2s.end();       // 停DMA与时钟
    recPathOpen = false;
    LOG_I("AP[录音] 录音路径已关闭(断电)");
}

bool AudioPlayer::applyRecGainLocked() {
    // 已持i2cMutex: 按输入源写左右ADC数字音量 —— 选中通路=用户增益(音量后的数据
    // 即写入WAV的数据, 电平表所见即所录), 未选中通路=0(数字静音, 悬空输入不进数据);
    // 双麦模式两路均为用户增益(立体声)。源切换/增益调整均走此函数, 监听态即时生效。
    int src = recSourceV;
    bool okL, okR;
    if (src == REC_SRC_BUILTIN) {
        okL = codec.setAdcLeftDigitalVolumeDB((float)recGainDb);   // 内置麦(LINPUT1)
        okR = codec.setAdcRightDigitalVolume(0);                   // 右路(耳机麦)静音
    } else if (src == REC_SRC_HEADSET) {
        okL = codec.setAdcLeftDigitalVolume(0);                    // 左路(内置麦)静音
        okR = codec.setAdcRightDigitalVolumeDB((float)recGainDb);  // 耳机麦(RINPUT1)
    } else {   // REC_SRC_BOTH: 内外双麦立体声(L=内置, R=外置)
        okL = codec.setAdcLeftDigitalVolumeDB((float)recGainDb);
        okR = codec.setAdcRightDigitalVolumeDB((float)recGainDb);
    }
    bool ok = okL && okR;
    LOG_I("AP[录音] 增益%ddB 源%d(0内置/1外置/2双) -> %s",
          (int)recGainDb, src, ok ? "OK" : "失败(NACK!)");
    return ok;
}

bool AudioPlayer::beginRecFile(const String& path) {
    // 落盘缓冲(录音期间独占, 停止后释放; 64KB常驻会挤占FLAC解码等大堆需求)
    if (!recBuf) {
        recBuf = (uint8_t*)malloc(REC_BUF_SIZE);
        if (!recBuf) {
            LOG_E("AP[录音] 缓冲分配失败%uKB(heap %u)",
                  (unsigned)(REC_BUF_SIZE / 1024), (unsigned)ESP.getFreeHeap());
            return false;
        }
    }
    recFile = SD_MMC.open(path, FILE_WRITE);
    if (!recFile) {
        LOG_E("AP[录音] 创建文件失败: %s", path.c_str());
        return false;
    }
    // 声道数按源定: 内置/耳机=单声道(88.2KB/s省空间), 双麦=立体声(176.4KB/s)
    recChannels = (recSourceV == REC_SRC_BOTH) ? 2 : 1;
    recFrameBytes = recChannels * 2;
    recFileOpen = true;
    recPath = path;
    uint8_t hdr[44];
    makeWavHeader(hdr, 0, AUDIO_SAMPLE_RATE, recChannels, 16);   // 占位头(长度0), 停止时回写
    recFile.write(hdr, 44);
    recBufPos = 0;
    recWritten = 0;
    recFrames = 0;
    recMuteUntil = millis() + 250;   // 丢弃起录瞬态(ADC稳定+建文件抖动)
    snapRecElapsed = 0;
    snapRecBytes = 44;
    LOG_I("AP[录音] 开始 %s (增益%ddB 源%d %uch)",
          path.c_str(), (int)recGainDb, (int)recSourceV, recChannels);
    return true;
}

void AudioPlayer::finishRecFile() {
    if (!recFileOpen) return;
    // 回写真实长度(WAV头两处: RIFF size与data size)
    uint8_t hdr[44];
    makeWavHeader(hdr, recWritten, AUDIO_SAMPLE_RATE, recChannels, 16);
    recFile.seek(0);
    recFile.write(hdr, 44);
    recFile.close();
    recFileOpen = false;
    LOG_I("AP[录音] 完成 %s (%uKB, %u.%02u秒 %uch)", recPath.c_str(),
          (unsigned)(recWritten / 1024),
          (unsigned)(recFrames / AUDIO_SAMPLE_RATE),
          (unsigned)(((uint64_t)recFrames * 100ULL / AUDIO_SAMPLE_RATE) % 100),
          recChannels);
}

void AudioPlayer::recProcessChunk(uint8_t* data, size_t& len) {
    // 按源处理一块DMA数据(交错立体声LRLR...):
    //   双麦(立体声): 逐帧累积L/R的VU与削波, 数据保持原样;
    //   单声道源: 取选中声道, VU的L/R同值(电平表两行一致), 削波双标志,
    //             并原地打包为连续单声道样本(LRLR...->LLL...)供WAV落盘, len减半
    int16_t* s = (int16_t*)data;
    size_t frames = len / 4;   // 交错立体声帧数
    int src = recSourceV;
    if (src == REC_SRC_BOTH) {
        for (size_t i = 0; i < frames; i++) {
            int32_t l = s[2 * i];
            int32_t r = s[2 * i + 1];
            uint16_t al = (uint16_t)(l < 0 ? -l : l);
            uint16_t ar = (uint16_t)(r < 0 ? -r : r);
            if (al > recVuMaxL) recVuMaxL = al;
            if (ar > recVuMaxR) recVuMaxR = ar;
            if (al >= REC_CLIP_TH) recClipHitL = true;
            if (ar >= REC_CLIP_TH) recClipHitR = true;
        }
    } else {
        bool useR = (src == REC_SRC_HEADSET);   // 耳机麦在右路
        for (size_t i = 0; i < frames; i++) {
            int16_t v = useR ? s[2 * i + 1] : s[2 * i];
            uint16_t a = (uint16_t)(v < 0 ? -v : v);
            if (a > recVuMaxL) recVuMaxL = a;   // 单声道: L/R电平一致
            if (a > recVuMaxR) recVuMaxR = a;
            if (a >= REC_CLIP_TH) {
                recClipHitL = true;             // 削波: 两个爆VU框同时示警
                recClipHitR = true;
            }
            s[i] = v;                           // 原地打包: 连续单声道样本
        }
        len = frames * 2;                       // 打包后长度(字节, 交错的一半)
    }
}

void AudioPlayer::flushRecData() {
    // 追赶式落盘: 从缓冲头部按块写SD, 每块写完穿插读DMA(新数据追加缓冲尾部)。
    // SD写速率(≥400KB/s)恒大于音频产生速率(单声道88.2/立体声176.4KB/s),
    // 写指针必然追上读指针; 穿插读取防止写卡期间DMA缓冲(仅5.7KB≈33ms)溢出丢数据。
    size_t done = 0;
    while (done < recBufPos) {
        size_t w = recBufPos - done;
        if (w > REC_SD_CHUNK) w = REC_SD_CHUNK;
        if (recFile.write(recBuf + done, w) != w) {
            LOG_E("AP[录音] TF写入失败(已写%uKB), 强制停止",
                  (unsigned)(recWritten / 1024));
            recError = true;
            recStateV = REC_IDLE;   // recordLoop退出并收尾(保留已录内容)
            return;
        }
        done += w;
        recWritten += w;

        // 穿插读DMA: 新数据接到缓冲尾部(空间由"追赶"保证, 峰值约为起始量1.5倍 < 64KB)
        size_t room = REC_BUF_SIZE - recBufPos;
        size_t want = (REC_READ_STEP < room) ? REC_READ_STEP : room;
        if (want > 0) {
            size_t got = 0;
            i2s_channel_read(i2s.rxChan(), recChunk, want, &got, 0);
            if (got > 0) {
                recProcessChunk(recChunk, got);   // got更新为打包后长度(单声道减半)
                // 收尾阶段(state已IDLE)或暂停中: 只算电平不写入
                if (recStateV == REC_RECORDING && (long)(millis() - recMuteUntil) >= 0) {
                    memcpy(recBuf + recBufPos, recChunk, got);
                    recBufPos += got;
                    recFrames += got / recFrameBytes;
                }
            }
        }
    }
    recBufPos = 0;   // 全部落盘, 缓冲清空
}

void AudioPlayer::recordLoop() {
    // 进入时录音路径已开; STANDBY/PAUSED均持续读DMA(保持电平表活动且DMA数据新鲜)
    while (recStateV != REC_IDLE) {
        // ---- 命令轮询(0超时; 非阻塞读保证命令延迟<5ms) ----
        AudioCmdMsg msg;
        while (xQueueReceive(cmdQueue, &msg, 0) == pdTRUE) {
            handleCommand(msg);
        }
        if (recStateV == REC_IDLE) break;   // STOP/错误已请求

        // ---- 非阻塞读DMA(timeout=0: 有多少读多少, 无数据立即返回) ----
        size_t got = 0;
        i2s_channel_read(i2s.rxChan(), recChunk, REC_READ_STEP, &got, 0);
        if (got > 0) {
            recProcessChunk(recChunk, got);   // got更新为打包后长度(单声道减半)
            // 仅"录音中"且过静音期才写入缓冲(监听/暂停态读后即弃)
            if (recStateV == REC_RECORDING && (long)(millis() - recMuteUntil) >= 0) {
                if (recBufPos + got <= REC_BUF_SIZE) {
                    memcpy(recBuf + recBufPos, recChunk, got);
                    recBufPos += got;
                    recFrames += got / recFrameBytes;
                } else {
                    recOverflow++;   // 仅落盘异常时出现(诊断用, 正常恒为0)
                }
            }
        }

        // ---- 50ms VU窗口 → 快照(复用播放VU快照, 播放录音互斥) ----
        unsigned long now = millis();
        if (now - lastVuAt >= 50) {
            lastVuAt = now;
            int l = vuToPct(recVuMaxL), r = vuToPct(recVuMaxR);
            recVuMaxL = 0;
            recVuMaxR = 0;
            snapVuL = (uint8_t)l;
            snapVuR = (uint8_t)r;
            snapPeakL = (l >= snapPeakL) ? (uint8_t)l : (uint8_t)(snapPeakL > 2 ? snapPeakL - 2 : 0);
            snapPeakR = (r >= snapPeakR) ? (uint8_t)r : (uint8_t)(snapPeakR > 2 ? snapPeakR - 2 : 0);
            // 削波(爆VU): 命中置位并保持1.5秒, 期间无新命中则自动清
            if (recClipHitL) { snapClipL = true; clipUntilL = now + 1500; recClipHitL = false; }
            else if ((long)(now - clipUntilL) >= 0) snapClipL = false;
            if (recClipHitR) { snapClipR = true; clipUntilR = now + 1500; recClipHitR = false; }
            else if ((long)(now - clipUntilR) >= 0) snapClipR = false;
            // 时长/文件大小快照(64位中间量防溢出)
            snapRecElapsed = (uint32_t)((uint64_t)recFrames * 1000ULL / AUDIO_SAMPLE_RATE);
            snapRecBytes = 44 + recWritten + recBufPos;
        }

        // ---- 缓冲过半: 追赶式落盘 ----
        if (recStateV == REC_RECORDING && recBufPos >= REC_BUF_FLUSH) {
            flushRecData();
        }

        // ---- 心跳探针(3秒) ----
        if (millis() - lastRecBeatMs > 3000) {
            lastRecBeatMs = millis();
            LOG_D("AP[录音] state=%d %us %uKB 缓冲=%u 溢出=%u 栈余=%u",
                  (int)recStateV, (unsigned)(snapRecElapsed / 1000),
                  (unsigned)(snapRecBytes / 1024), (unsigned)recBufPos,
                  (unsigned)recOverflow,
                  (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }

        if (got == 0) vTaskDelay(1);   // DMA暂无数据: 1ms后再查(不忙等)
    }

    // ---- 收尾: 剩余落盘 → WAV头回写 → 关文件 → 关录音路径(断电) ----
    if (recFileOpen) {
        flushRecData();
        finishRecFile();
    }
    closeRecordPath();
    if (recBuf) {   // 释放落盘缓冲(用完即还, 不挤占其他功能堆内存)
        free(recBuf);
        recBuf = nullptr;
    }
    // 清VU快照防残留(页面返回流程只看recState)
    snapVuL = 0; snapVuR = 0;
    snapPeakL = 0; snapPeakR = 0;
    snapClipL = false; snapClipR = false;
    LOG_I("AP[录音] 引擎已退出(路径关闭)");
}

// ================= 录音命令接口(UI任务调用, 非阻塞入队) =================
bool AudioPlayer::armRecording() {
    if (recStateV != REC_IDLE) return true;   // 已在监听/录音
    if (!cmdQueue) {
        LOG_E("AP[命令] 监听失败: 音频任务未初始化");
        return false;
    }
    AudioCmdMsg msg = { CMD_REC_ARM, 0, nullptr, nullptr };
    if (xQueueSend(cmdQueue, &msg, 0) != pdTRUE) {
        LOG_W("AP[命令] 监听命令入队失败(队列满)");
        return false;
    }
    return true;
}

bool AudioPlayer::startRecording(const String& path) {
    if (recStateV == REC_RECORDING) return true;   // 已在录
    if (!cmdQueue) {
        LOG_E("AP[命令] 录音失败: 音频任务未初始化");
        return false;
    }
    // path指向页面成员String, 存活期覆盖命令处理(音频任务50ms内同步拷贝)
    AudioCmdMsg msg = { CMD_REC_START, 0, nullptr, &path };
    if (xQueueSend(cmdQueue, &msg, 0) != pdTRUE) {
        LOG_W("AP[命令] 录音命令入队失败(队列满)");
        return false;
    }
    return true;
}

void AudioPlayer::pauseRecording() {
    if (recStateV != REC_RECORDING) return;
    AudioCmdMsg msg = { CMD_REC_PAUSE, 0, nullptr, nullptr };
    postCmd(cmdQueue, msg, "REC_PAUSE");
}

void AudioPlayer::resumeRecording() {
    if (recStateV != REC_PAUSED) return;
    AudioCmdMsg msg = { CMD_REC_RESUME, 0, nullptr, nullptr };
    postCmd(cmdQueue, msg, "REC_RESUME");
}

void AudioPlayer::stopRecording() {
    if (recStateV == REC_IDLE) return;
    AudioCmdMsg msg = { CMD_REC_STOP, 0, nullptr, nullptr };
    postCmd(cmdQueue, msg, "REC_STOP");
}

void AudioPlayer::setRecGain(int db) {
    if (db < REC_GAIN_MIN) db = REC_GAIN_MIN;
    if (db > REC_GAIN_MAX) db = REC_GAIN_MAX;
    recGainDb = db;   // UI立即读到新值(百分比/dB显示)
    // 与setVolume同策略: 队列满且对讲/录音路径已开时, 跨任务同步补写一次(I2C锁保护)
    AudioCmdMsg msg = { CMD_REC_VOL, 0, nullptr, nullptr };
    if (cmdQueue && xQueueSend(cmdQueue, &msg, 0) != pdTRUE) {
        LOG_W("AP[命令] 增益命令入队失败，立即重试");
        if (recPathOpen || talkPathOpen) {
            codecI2cLock();
            if (talkPathOpen && !recPathOpen) {
                codec.setAdcLeftDigitalVolumeDB((float)recGainDb);   // TALK: 左麦
                codec.setAdcRightDigitalVolume(0);
            } else {
                applyRecGainLocked();
            }
            codecI2cUnlock();
        }
    }
}

void AudioPlayer::setRecSource(int src) {
    if (src < REC_SRC_BUILTIN || src > REC_SRC_BOTH) src = REC_SRC_DEFAULT;
    recSourceV = src;   // UI立即读到新值(显示); 音频任务的mono复制volatile读即时生效
    // 入队让音频任务重写左右ADC音量(选中=增益/未选=静音), 监听电平表即时切换
    AudioCmdMsg msg = { CMD_REC_SRC, src, nullptr, nullptr };
    if (cmdQueue && xQueueSend(cmdQueue, &msg, 0) != pdTRUE) {
        LOG_W("AP[命令] 切源命令入队失败，立即重试");
        if (recPathOpen) {
            codecI2cLock();
            applyRecGainLocked();
            codecI2cUnlock();
        }
    }
}

// ================= 对讲命令接口(UI任务调用, 全部非阻塞入队) =================
bool AudioPlayer::startTalk() {
    if (talkStateV != TALK_IDLE) return true;   // 已在通话
    if (!cmdQueue) {
        LOG_E("AP[命令] 对讲失败: 音频任务未初始化");
        return false;
    }
    AudioCmdMsg msg = {};
    msg.cmd = CMD_TALK_START;
    if (xQueueSend(cmdQueue, &msg, 0) != pdTRUE) {
        LOG_W("AP[命令] 对讲启动入队失败(队列满)");
        return false;
    }
    return true;
}

void AudioPlayer::stopTalk() {
    if (talkStateV == TALK_IDLE) return;
    AudioCmdMsg msg = {};
    msg.cmd = CMD_TALK_STOP;
    postCmd(cmdQueue, msg, "TALK_STOP");
}

void AudioPlayer::setTalkPtt(bool talking) {
    talkPttV = talking;   // UI立即读到新值(TX灯); 音频任务读volatile即时生效
    AudioCmdMsg msg = {};
    msg.cmd = CMD_TALK_PTT;
    msg.index = talking ? 1 : 0;
    if (cmdQueue) xQueueSend(cmdQueue, &msg, 0);
}

void AudioPlayer::setTalkMuted(bool muted) {
    talkMutedV = muted;
    AudioCmdMsg msg = {};
    msg.cmd = CMD_TALK_MUTE;
    msg.index = muted ? 1 : 0;
    if (cmdQueue) xQueueSend(cmdQueue, &msg, 0);
}

void AudioPlayer::setTalkMode(int mode) {
    talkModeV = (mode == 1) ? 1 : 0;
    AudioCmdMsg msg = {};
    msg.cmd = CMD_TALK_MODE;
    msg.index = talkModeV;
    if (cmdQueue) xQueueSend(cmdQueue, &msg, 0);
}

// ================= 对讲引擎(仅音频任务) =================
// 硬件恒跑44.1k全双工(I2S+WM8960录音/播放同构通路); 软件重采样到8k/16k做ADPCM。
// PTT=16k帧(384样本=24ms, 约64kbps, 约42pps); 全双工=8k帧(384样本=48ms, 约32kbps)。
// MIC取左路内置麦; RX解码上采样到44.1k后双声道同值写入DAC。
bool AudioPlayer::openTalkPath() {
    if (talkPathOpen) return true;
    if (audioPathOpen) closeAudioPath();   // 播放/对讲互斥(共用I2S与WM8960)
    if (recPathOpen) closeRecordPath();    // 录音/对讲互斥

    // I2S全双工: 与录音路径同款引脚+44.1k/16bit/立体声
    i2s.setPins(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_DIN, PIN_I2S_MCLK);
    if (!i2s.begin(I2S_MODE_STD, AUDIO_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
        LOG_E("AP[对讲] I2S初始化失败");
        return false;
    }
    i2s.setTimeout(I2S_WRITE_TIMEOUT_MS);
    gpio_set_direction((gpio_num_t)PIN_I2S_ALRC, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(PIN_I2S_ALRC, I2S0O_WS_OUT_IDX, false, false);

    {
        codecI2cLock();
        bool ok = codec.begin(Wire);
        if (ok) {
            codec.enablePgaZeroCross();
            codec.enableVREF();
            codec.setVMID(WM8960_VMIDSEL_2X250KOHM);
        }
        codecI2cUnlock();
        if (!ok) {
            LOG_E("AP[对讲] WM8960无响应");
            i2s.end();
            return false;
        }

        delay(700);   // VMID慢充电(锁外)

        codecI2cLock();
        codec.enablePeripheralMode();
        codec.setCLKSEL(WM8960_CLKSEL_MCLK);
        codec.setSYSCLKDIV(WM8960_SYSCLK_DIV_BY_1);
        codec.setADCDIV(0);
        codec.setDACDIV(0);
        codec.setDCLKDIV(WM8960_DCLKDIV_16);
        codec.setWL(WM8960_WL_16BIT);
        // 输入链(同录音: 内置麦LINPUT1单端→左PGA→Boost→ADC)
        codec.setAdcLeftDigitalVolume(0);    // 先静音, 稳定后再开
        codec.setAdcRightDigitalVolume(0);
        codec.enableMicBias();
        codec.setMicBiasVoltage(WM8960_MIC_BIAS_VOLTAGE_0_9_AVDD);
        codec.enableLMIC();
        codec.enableRMIC();
        codec.connectLMN1();
        codec.connectRMN1();
        codec.pgaLeftNonInvSignalSelect(WM8960_PGAL_VMID);
        codec.pgaRightNonInvSignalSelect(WM8960_PGAR_VMID);
        codec.disableLINMUTE();
        codec.disableRINMUTE();
        codec.setLINVOLDB(20.0f);
        codec.setRINVOLDB(20.0f);
        codec.setLMICBOOST(WM8960_MIC_BOOST_GAIN_13DB);
        codec.setRMICBOOST(WM8960_MIC_BOOST_GAIN_13DB);
        codec.connectLMIC2B();
        codec.connectRMIC2B();
        codec.enableAINL();
        codec.enableAINR();
        codec.enableAdcLeft();    // 左路=内置麦(对讲MIC)
        codec.enableAdcRight();
        // 输出链(同播放: DAC→耳机+喇叭, 喇叭跟随用户speakerOn开关)
        codec.enableDacLeft();
        codec.enableDacRight();
        codec.enableLD2LO();
        codec.enableRD2RO();
        codec.enableLOMIX();
        codec.enableROMIX();
        codec.enableOUT3asVMID();
        codec.enableOUT3MIX();
        codec.enableHeadphones();
        if (speakerOn) codec.enableSpeakers();
        codecI2cUnlock();

        delay(100);   // 链路稳定(锁外)

        codecI2cLock();
        codec.setVMID(WM8960_VMIDSEL_2X50KOHM);
        codec.antiPopPowerUpRelease();
        // 左路按用户增益开MIC, 右路静音(悬空耳机麦不进数据)
        codec.setAdcLeftDigitalVolumeDB((float)recGainDb);
        codec.setAdcRightDigitalVolume(0);
        rampVolumeForTargetLocked();   // 输出模拟音量软斜坡到目标
        codecI2cUnlock();
    }

    primeSilentFrames();   // DMA跑稳后再解mute
    codecI2cLock();
    codec.disableDacMute();
    codecI2cUnlock();

    talkPathOpen = true;
    talkRsAcc = 0; talkRsLast = 0;
    talkRsCount = 0;
    talkUsAcc = 0; talkUsPos = 0; talkUsLen = 0; talkUsLast = 0;
    talkEncPred = 0; talkEncIdx = 0;
    talkDecPred = 0; talkDecIdx = 0;
    lastVuAt = 0;
    LOG_I("AP[对讲] 双工路径已开启(MIC左路+耳机/喇叭输出)");
    return true;
}

void AudioPlayer::closeTalkPath() {
    if (!talkPathOpen) return;
    // 软关断: ADC静音→输出静音→逐级断电(同录音/播放语义, 防"嘭"声)
    codecI2cLock();
    codec.setAdcLeftDigitalVolume(0);
    codec.setAdcRightDigitalVolume(0);
    codec.disableAdcLeft();
    codec.disableAdcRight();
    codec.disableAINL();
    codec.disableAINR();
    codec.disableLMIC();
    codec.disableRMIC();
    codec.disableMicBias();
    codec.enableDacMute();
    codec.setHeadphoneVolume(0);
    codec.setSpeakerVolume(0);
    codecI2cUnlock();
    delay(30);
    codecI2cLock();
    codec.disableHeadphones();
    codec.disableSpeakers();
    codec.disableLOMIX();
    codec.disableROMIX();
    codec.disableOUT3MIX();
    codec.disableDacLeft();
    codec.disableDacRight();
    codec.setVMID(WM8960_VMIDSEL_2X250KOHM);
    codecI2cUnlock();
    delay(50);
    codecI2cLock();
    codec.disableVMID();
    codec.disableVREF();
    codecI2cUnlock();
    i2s.end();
    talkPathOpen = false;
    LOG_I("AP[对讲] 双工路径已关闭");
}

// MIC处理: 立体声DMA块→取左路单声道→重采样到目标率→攒384样本→ADPCM→TX环形缓冲
void AudioPlayer::talkProcessMic(const int16_t* stereo, int frames) {
    // 目标率: PTT=16k / 全双工=8k
    uint32_t target = talkModeV ? 8000 : 16000;
    for (int i = 0; i < frames; i++) {
        int16_t s = stereo[2 * i];   // 左路=内置麦
        // 分数步进重采样(44.1k→target): 每步acc+=target, 满44100输出一帧。
        // 相位frac=talkRsAcc/44100∈[0,1): 输出=last+(s-last)*frac(线性插值)。
        talkRsAcc += target;
        while (talkRsAcc >= (uint32_t)AUDIO_SAMPLE_RATE) {
            uint32_t frac = talkRsAcc - AUDIO_SAMPLE_RATE;
            talkRsAcc = frac;
            int32_t v = (int32_t)talkRsLast + ((int32_t)(s - talkRsLast) *
                        (int32_t)frac / AUDIO_SAMPLE_RATE);
            talkRsLast = s;
            // 攒帧
            talkRsBuf[talkRsCount++] = (int16_t)v;
            if (talkRsCount >= TALK_VOICE_SAMPLES) {
                talkRsCount = 0;
                // ADPCM编码(连续流state)
                TalkAdpcmState encSt = { talkEncPred, talkEncIdx };
                talkAdpcmEncode(encSt, talkRsBuf, TALK_VOICE_SAMPLES, talkAdpcmBuf);
                talkEncPred = encSt.predictor;
                talkEncIdx = encSt.stepIndex;
                // 是否发送: PTT需按下且未静音; 全双工只需未静音(常开);
                // 另需对端仍在连接中(掉线看门狗已置READY后不再空发语音)
                bool wantTx = talkMutedV ? false :
                              (talkModeV ? true : talkPttV);
                if (wantTx && talkManager.isPeerConnected()) {
                    talkManager.pushTxFrame(talkAdpcmBuf,
                        talkModeV ? TALK_CODEC_ADPCM8K : TALK_CODEC_ADPCM16K, false);
                }
            }
        }
    }
}

// SPK处理: RX环形缓冲取一帧→ADPCM解码→上采样到44.1k→双声道同值写入播放帧。
// 无RX帧时输出静音(保持时钟连续, 不写旧数据防重复杂音)。
// 上采样分数步进: 每消费1个源样本产出44100/target个输出(线性插值)。
void AudioPlayer::talkProcessSpk(int16_t* stereo, int frames) {
    for (int i = 0; i < frames; i++) {
        if (talkUsPos >= talkUsLen) {
            uint8_t adpcm[TALK_VOICE_FRAME_BYTES];
            if (talkManager.popRxFrame(adpcm)) {
                // 远端帧: 解码到talkPcmBuf(384样本@8k/16k)
                // 编码端: PTT=16k / 全双工=8k; 本端talkModeV与对端一般一致(CTRL同步),
                // 但MODE切换在途时可能短暂不一致 → 用音频任务本地talkModeV判率
                // (错判一帧只影响该帧音调, 下一帧即恢复, 比传codec更鲁棒)。
                TalkAdpcmState decSt = { talkDecPred, talkDecIdx };
                talkAdpcmDecode(decSt, adpcm, TALK_VOICE_FRAME_BYTES, talkPcmBuf);
                talkDecPred = decSt.predictor;
                talkDecIdx = decSt.stepIndex;
                for (int k = 0; k < TALK_VOICE_SAMPLES; k++) {
                    int32_t a = talkPcmBuf[k];
                    if (a < 0) a = -a;
                    if (a > talkRxMax) talkRxMax = (uint16_t)a;
                }
                uint32_t target = talkModeV ? 8000 : 16000;
                int out = 0;
                int16_t prev = talkUsLast;
                const int SPK_CAP = (int)(sizeof(talkSpkBuf) / sizeof(talkSpkBuf[0]));
                for (int k = 0; k < TALK_VOICE_SAMPLES && out < SPK_CAP; k++) {
                    int16_t s = talkPcmBuf[k];
                    talkUsAcc += (uint32_t)AUDIO_SAMPLE_RATE;
                    while (talkUsAcc >= target && out < SPK_CAP) {
                        // 相位frac=talkUsAcc/target∈[0,1): 输出=prev+(s-prev)*frac
                        uint32_t frac = talkUsAcc - target;
                        talkUsAcc = frac;
                        int32_t v = (int32_t)prev + ((int32_t)(s - prev) * (int32_t)frac / (int32_t)target);
                        talkSpkBuf[out++] = (int16_t)v;
                    }
                    prev = s;
                }
                talkUsLast = talkPcmBuf[TALK_VOICE_SAMPLES - 1];
                talkUsPos = 0;
                talkUsLen = out;
            } else {
                // 无帧: 本轮剩余全部静音(时钟连续, 不消费旧数据)
                for (int j = i; j < frames; j++) {
                    stereo[2 * j] = 0;
                    stereo[2 * j + 1] = 0;
                }
                talkUsPos = 0;
                talkUsLen = 0;
                return;
            }
        }
        int16_t v = (talkUsPos < talkUsLen) ? talkSpkBuf[talkUsPos++] : 0;
        stereo[2 * i] = v;
        stereo[2 * i + 1] = v;
    }
}

// 对讲主循环(音频任务): 命令轮询→非阻塞读DMA→MIC编码→SPK解码混音→阻塞写DMA→VU快照。
// PTT模式: 未按下时MIC仍读(保电平表活动)但不编码发送, SPK始终播放对端。
// talkStateV回IDLE即收尾关路径, 回主循环。
void AudioPlayer::talkLoop() {
    // 对讲DMA块: 512帧立体声(约11.6ms@44.1k), 与录音REC_READ_STEP=2048B同量级
    static const int TALK_DMA_FRAMES = 512;
    static int16_t dmaBuf[512][2];
    // RX首包看门狗: 全双工下若对端只发不收也有声, 此处仅做心跳诊断
    while (talkStateV != TALK_IDLE) {
        // ---- 命令轮询(0超时, 延迟<5ms) ----
        AudioCmdMsg msg;
        while (xQueueReceive(cmdQueue, &msg, 0) == pdTRUE) {
            handleCommand(msg);
        }
        if (talkStateV == TALK_IDLE) break;

        // ---- 非阻塞读MIC DMA ----
        size_t got = 0;
        i2s_channel_read(i2s.rxChan(), (uint8_t*)dmaBuf, sizeof(dmaBuf), &got, 0);
        int frames = (int)(got / 4);   // 立体声帧(4B/帧)
        if (frames > TALK_DMA_FRAMES) frames = TALK_DMA_FRAMES;

        // VU: 取左路最大幅值(50ms窗口→快照, L/R同值, 复用播放VU快照)
        for (int i = 0; i < frames; i++) {
            int32_t v = dmaBuf[i][0];
            uint16_t a = (uint16_t)(v < 0 ? -v : v);
            if (a > recVuMaxL) recVuMaxL = a;   // 借用录音VU累积器(对讲与录音互斥)
            if (a > recVuMaxR) recVuMaxR = a;
        }

        if (frames > 0) {
            // MIC→编码→TX(全双工回声门: 本端RX活跃且MIC弱→疑似喇叭回灌, 跳过编码)
            // 用本块瞬时MIC峰值判回声(不用recVuMaxL: 那是50ms窗口累积器, 保留了
            // 旧块峰值, 会让"刚说完话→只余回灌"的块被误判有声, 回声漏网)
            int32_t micPeak = 0;
            for (int i = 0; i < frames; i++) {
                int32_t a = dmaBuf[i][0];
                if (a < 0) a = -a;
                if (a > micPeak) micPeak = a;
            }
            bool echoGate = false;
            if (talkModeV && talkEchoSuppressV) {
                // RX近200ms有声 且 本块MIC峰值<门限 → 回声, 不编码(省带宽+断正反馈)
                echoGate = talkManager.hasRxVoice(200) && (micPeak < TALK_ECHO_MIC_TH);
            }
            if (!echoGate) talkProcessMic((const int16_t*)dmaBuf, frames);
            // RX→解码→混入(原地替换dmaBuf为播放帧)
            talkProcessSpk((int16_t*)dmaBuf, frames);
        } else {
            // DMA暂无数据: 填静音帧(保时钟连续)
            memset(dmaBuf, 0, sizeof(dmaBuf));
            frames = TALK_DMA_FRAMES;
        }

        // ---- 阻塞写DAC DMA(100ms超时, 背压即让出) ----
        size_t want = (size_t)frames * 4;
        size_t wrote = i2s.write((const uint8_t*)dmaBuf, want);
        if (wrote < want) apWriteFailCount++;

        // ---- 50ms VU窗口→快照 ----
        unsigned long now = millis();
        if (now - lastVuAt >= 50) {
            lastVuAt = now;
            int l = vuToPct(recVuMaxL), r = vuToPct(recVuMaxR);
            recVuMaxL = 0;
            recVuMaxR = 0;
            snapVuL = (uint8_t)l;
            snapVuR = (uint8_t)r;
            snapPeakL = (l >= snapPeakL) ? (uint8_t)l : (uint8_t)(snapPeakL > 2 ? snapPeakL - 2 : 0);
            snapPeakR = (r >= snapPeakR) ? (uint8_t)r : (uint8_t)(snapPeakR > 2 ? snapPeakR - 2 : 0);
            // RX喇叭电平: 有远端帧时按解码峰值, 无帧时快速衰减到0
            int spk = vuToPct(talkRxMax);
            talkRxMax = 0;
            if (spk >= snapSpk) snapSpk = (uint8_t)spk;
            else snapSpk = (snapSpk > 12) ? (uint8_t)(snapSpk - 12) : 0;
        }

        // ---- 心跳(3秒) ----
        if (millis() - lastTalkBeatMs > 3000) {
            lastTalkBeatMs = millis();
            LOG_D("AP[对讲] %s %s ptt=%d mute=%d 栈余=%u",
                  talkModeV ? "FULL(8k)" : "PTT(16k)",
                  talkPathOpen ? "open" : "closed",
                  (int)talkPttV, (int)talkMutedV,
                  (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }

        if (got == 0) vTaskDelay(1);
    }

    // ---- 收尾: 关路径+清VU ----
    closeTalkPath();
    snapVuL = 0; snapVuR = 0;
    snapSpk = 0;
    talkRxMax = 0;
    snapPeakL = 0; snapPeakR = 0;
    talkRsCount = 0;
    talkRsAcc = 0; talkUsAcc = 0; talkUsPos = 0; talkUsLen = 0;
    LOG_I("AP[对讲] 引擎已退出(路径关闭)");
}

AudioGenerator* AudioPlayer::createDecoder(const String& lowerName) const {
    if (lowerName.endsWith(".mp3"))  return new AudioGeneratorMP3();
    if (lowerName.endsWith(".wav"))  return new AudioGeneratorWAV();
    if (lowerName.endsWith(".flac")) return new AudioGeneratorFLAC();
    if (lowerName.endsWith(".aac"))  return new AudioGeneratorAAC();
    if (lowerName.endsWith(".opus") || lowerName.endsWith(".ogg")) return new AudioGeneratorOpus();
    // MIDI 需要外部 .sf2 音色库(库无内置)，未接音色库时 begin() 会空指针崩溃，故禁用
    if (lowerName.endsWith(".mod"))  return new AudioGeneratorMOD();
    return nullptr;
}

bool AudioPlayer::isAudioFile(const String& name) {
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".mp3") || lower.endsWith(".wav") || lower.endsWith(".flac") ||
           lower.endsWith(".aac") || lower.endsWith(".opus") || lower.endsWith(".ogg") ||
           lower.endsWith(".mod");
}

// ================= 总时长+采样率+位深解析(仅音频任务调用) =================
uint32_t AudioPlayer::estimateTotalMs(const String& path, const String& lowerExt,
                                      uint32_t* outRate, uint16_t* outBits) const {
    if (outRate) *outRate = 0;
    if (outBits) *outBits = 0;
    if (!cardReady) return 0;
    File f = SD_MMC.open(path, "r");
    if (!f) return 0;

    uint8_t hdr[64];
    size_t got = f.read(hdr, sizeof(hdr));
    uint32_t fsize = f.size();
    f.close();
    if (got < 16) return 0;

    uint32_t ms = 0;

    if (lowerExt.endsWith(".wav")) {
        if (memcmp(hdr, "RIFF", 4) == 0 && memcmp(hdr + 8, "WAVE", 4) == 0) {
            uint32_t byteRate = 0, dataSize = 0, sampleRate = 0;
            for (size_t i = 12; i + 8 <= got; ) {
                uint32_t sz = hdr[i + 4] | (hdr[i + 5] << 8) | (hdr[i + 6] << 16) | ((uint32_t)hdr[i + 7] << 24);
                if (memcmp(hdr + i, "fmt ", 4) == 0 && i + 16 <= got) {
                    sampleRate = hdr[i + 8] | (hdr[i + 9] << 8) | (hdr[i + 10] << 16) | ((uint32_t)hdr[i + 11] << 24);
                    byteRate = hdr[i + 12] | (hdr[i + 13] << 8) | (hdr[i + 14] << 16) | ((uint32_t)hdr[i + 15] << 24);
                    if (outBits && i + 16 + 2 <= got) {   // 位深在fmt块内偏移14(16位LE)
                        uint16_t bits = hdr[i + 14] | (hdr[i + 15] << 8);
                        if (bits >= 8 && bits <= 32) *outBits = bits;
                    }
                } else if (memcmp(hdr + i, "data", 4) == 0) {
                    dataSize = sz;
                }
                i += 8 + sz + (sz & 1);
            }
            if (outRate) *outRate = sampleRate;
            if (byteRate > 0 && dataSize > 0) {
                ms = (uint32_t)((uint64_t)dataSize * 1000ULL / byteRate);
            } else if (byteRate > 0) {
                ms = (uint32_t)((uint64_t)fsize * 1000ULL / byteRate);
            }
        }
    } else if (lowerExt.endsWith(".mp3")) {
        uint32_t start = 0;
        if (memcmp(hdr, "ID3", 3) == 0) {
            start = 10 + (((hdr[6] & 0x7F) << 21) | ((hdr[7] & 0x7F) << 14) |
                          ((hdr[8] & 0x7F) << 7) | (hdr[9] & 0x7F));
        }
        if (start + 64 <= fsize) {
            File f2 = SD_MMC.open(path, "r");
            if (f2) {
                f2.seek(start);
                uint8_t fb[64];
                if (f2.read(fb, sizeof(fb)) == sizeof(fb)) {
                    for (int off = 0; off + 48 <= (int)sizeof(fb); off++) {
                        if (fb[off] != 0xFF || (fb[off + 1] & 0xE0) != 0xE0 || ((fb[off + 1] >> 1) & 0x03) != 1) {
                            continue;
                        }
                        uint8_t verBits = (fb[off + 1] >> 3) & 0x03;
                        uint8_t brIdx = fb[off + 2] >> 4;
                        uint8_t srIdx = (fb[off + 2] >> 2) & 0x03;
                        bool mono = ((fb[off + 3] >> 6) & 0x03) == 3;
                        int sideInfo = (verBits == 3) ? (mono ? 17 : 32) : (mono ? 9 : 17);
                        const uint8_t* xp = fb + off + 4 + sideInfo;
                        static const uint32_t srTab[3][3] = {
                            {11025, 12000, 8000}, {22050, 24000, 16000}, {44100, 48000, 32000}
                        };
                        int row = (verBits == 3) ? 2 : (verBits == 2 ? 1 : 0);
                        if (srIdx < 3) {
                            if (outRate) *outRate = srTab[row][srIdx];
                        }
                        if (memcmp(xp, "Xing", 4) == 0 || memcmp(xp, "Info", 4) == 0) {
                            uint32_t flags = ((uint32_t)xp[4] << 24) | ((uint32_t)xp[5] << 16) |
                                             ((uint32_t)xp[6] << 8) | xp[7];
                            if (flags & 1) {
                                uint32_t frames = ((uint32_t)xp[8] << 24) | ((uint32_t)xp[9] << 16) |
                                                  ((uint32_t)xp[10] << 8) | xp[11];
                                if (srIdx < 3 && frames > 0) {
                                    uint32_t sr = srTab[row][srIdx];
                                    int spf = (verBits == 3) ? 1152 : 576;
                                    ms = (uint32_t)((uint64_t)frames * spf * 1000ULL / sr);
                                }
                            }
                        }
                        if (ms == 0 && brIdx > 0 && brIdx < 15) {
                            static const uint16_t brV1[15] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};
                            static const uint16_t brV2[15] = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160};
                            uint16_t kbps = (verBits == 3) ? brV1[brIdx] : brV2[brIdx];
                            if (kbps > 0 && fsize > start + off) {
                                ms = (uint32_t)((uint64_t)(fsize - start - off) * 8000ULL / kbps);
                            }
                        }
                        break;
                    }
                }
                f2.close();
            }
        }
    } else if (lowerExt.endsWith(".flac")) {
        if (memcmp(hdr, "fLaC", 4) == 0 && got >= 26 && (hdr[4] & 0x7F) == 0) {
            uint32_t sampleRate = ((uint32_t)hdr[18] << 12) | ((uint32_t)hdr[19] << 4) | (hdr[20] >> 4);
            uint64_t totalSamples = ((uint64_t)(hdr[21] & 0x0F) << 32) |
                                    ((uint64_t)hdr[22] << 24) | ((uint64_t)hdr[23] << 16) |
                                    ((uint64_t)hdr[24] << 8) | hdr[25];
            uint16_t bits = (uint16_t)(((hdr[20] & 0x0F) << 1) | (hdr[21] >> 7));  // STREAMINFO位深(5bit)
            if (outBits && bits >= 8 && bits <= 32) *outBits = bits;
            if (outRate) *outRate = sampleRate;
            if (sampleRate > 0 && totalSamples > 0) {
                ms = (uint32_t)(totalSamples * 1000ULL / sampleRate);
            }
        }
    }

    return ms;
}

// 幅值->电平百分比: -60..0 dB线性映射到 0..100 (VU表风格, 低电平更敏感)
int AudioPlayer::vuToPct(uint16_t amp) {
    if (amp < 8) return 0;                       // 约-72dB以下视为静音
    float v = amp / 32767.0f;
    float db = 20.0f * log10f(v);
    float pct = (db + 60.0f) / 60.0f * 100.0f;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (int)pct;
}
