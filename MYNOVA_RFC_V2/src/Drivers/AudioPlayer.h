/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// AudioPlayer.h
// WM8960 + I2S + TF卡 多格式音频播放/录音引擎 (单任务架构, 与USBTFCard参考工程同构)
//
// ================= 架构 =================
//
//   UI任务(按键/渲染)                 音频任务(唯一, Core0)
//   ─────────────────                ─────────────────────
//   onButtonXxx:                     while(true) {
//     命令入队(非阻塞)──命令队列──▶     轮询命令队列:
//                                     VOL/PAUSE/RESUME: 直接I2C写WM8960(立即生效)
//                                     PLAY/STOP/LOAD: 同步执行demo同款流程
//                                       停旧: decoder->stop()+delete(官方方法, 安全)
//                                       开新: 文件头解析→采样率预配→begin
//   读快照(volatile) ◀──每轮更新──     if PLAYING: decoder->loop()
//                                     if REC*: recordLoop()(I2S读取→VU→TF卡写入)
//                                   }
//
//   关键设计(与demo一致):
//     - 单任务串行: 解码器/文件/WM8960/I2S 全归音频任务, 切歌=stop+new, 无跨任务死锁
//     - 播放列表: 浏览页传入同层音频文件列表(支持随机/循环模式), 切歌=列表内取路径
//     - 录音(与播放互斥): 差分麦克风→PGA(+20dB)→Boost(+13dB)→ADC→I2S RX→TF卡WAV
//       用户可调增益=ADC数字音量(-60~+30dB), 录到的电平与电平表所见一致(所见即所得)
//
// 数据流: TF卡(SD_MMC) -> AudioFileSourceFS -> AudioGenerator解码 -> I2S -> WM8960 -> 耳机/喇叭
#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <Arduino.h>
#include <ESP_I2S.h>
#include <SD_MMC.h>
#include <vector>
#include "../Lib/WM8960_Lib.h"
#include "AudioFileSource.h"
#include "AudioOutput.h"
#include "../Proto/TalkProto.h"
#include "../Proto/TalkCodec.h"
// 注: 不include TalkManager.h(其含WiFi/esp_now, AudioPlayer.h被多处包含会拖慢编译);
// talkManager引用在AudioPlayer.cpp中include TalkManager.h后使用。
class TalkManager;

// 播放器状态(快照)
enum PlayerState {
    PLAYER_IDLE,      // 空闲(音频路径已关闭)
    PLAYER_STARTING,  // 正在切歌/开启
    PLAYER_PLAYING,   // 播放中
    PLAYER_PAUSED     // 已暂停(静音+耳机/喇叭断电, 解码暂停, 时间冻结)
};

// 循环播放模式
enum PlayMode {
    PLAYMODE_SEQUENCE = 0,   // 顺序播放(列表播完停止)
    PLAYMODE_REPEAT_ALL,     // 列表循环
    PLAYMODE_REPEAT_ONE,     // 单曲循环
    PLAYMODE_RANDOM          // 随机播放
};

// 初始化结果
enum AudioInitState {
    AUDIO_INIT_NONE,     // 尚未初始化
    AUDIO_INIT_OK,       // TF卡与WM8960均在线
    AUDIO_INIT_NO_CARD,  // TF卡挂载失败(未插卡)
    AUDIO_INIT_NO_CODEC  // WM8960无响应(可浏览不能播放)
};

// 录音状态(快照; 与播放状态互斥: 录音期间PlayerState保持PLAYER_IDLE)
enum RecState {
    REC_IDLE,        // 未录音(录音路径已关闭)
    REC_STANDBY,     // 监听待机(录音路径已开, 电平表活动, 未写文件)
    REC_RECORDING,   // 录音中(WAV写入TF卡)
    REC_PAUSED       // 录音暂停(继续监听电平, 不写文件, 时长冻结)
};

// 录音输入源(硬件: LINPUT1=内置麦克风AOS3729A, RINPUT1=耳机麦克风(外置, 可能未插))
// 单声道源录单声道WAV(88.2KB/s省空间, 电平表L/R一致显示), 双麦录立体声WAV(176.4KB/s)
enum RecSource {
    REC_SRC_BUILTIN = 0,   // 内置麦克风(左路, 单声道)
    REC_SRC_HEADSET = 1,   // 外置耳机麦克风(右路, 单声道)
    REC_SRC_BOTH = 2       // 内外双麦克风(立体声: L=内置, R=外置)
};
#define REC_SRC_DEFAULT  REC_SRC_BUILTIN

// 录音参数(用户增益=ADC数字音量, 直接决定写入WAV的电平)
#define REC_GAIN_MIN      (-60)   // dB
#define REC_GAIN_MAX      (30)    // dB
#define REC_GAIN_STEP     (3)     // 按键步进dB
#define REC_GAIN_DEFAULT  (-12)   // 默认增益(留余量防削波)

// 对讲通话状态(快照; 与播放/录音互斥: TALK期间PlayerState保持PLAYER_IDLE,
// RecState保持REC_IDLE; talkLoop接管音频任务, 退出即收尾关路径)
enum TalkState {
    TALK_IDLE = 0,    // 未通话
    TALK_ACTIVE       // 通话中(PTT/全双工由TalkManager的mode区分)
};

// 全双工回声门: RX活跃(对端在讲)且本端MIC弱→疑似喇叭回灌, 跳过本块编码。
// 阈值取幅值约-40dB(32767*0.01≈330): 正常讲话近场MIC远大于此, 喇叭经空气回灌衰减大。
#define TALK_ECHO_MIC_TH  330

class AudioGenerator;
class AudioFileSourceFS;

// ================= 音频输出适配器: 解码器PCM -> I2S =================
// (仅音频任务访问; write带超时, DMA停摆时快速失败)
class AudioOutputWM8960 : public AudioOutput {
public:
    AudioOutputWM8960(I2SClass *i2s);

    bool SetRate(int hz) override;       // 采样率变化时重配I2S时钟
    bool begin() override;               // 新曲目: 重置时长计数
    bool ConsumeSample(int16_t sample[2]) override;
    bool stop() override;

    uint32_t getElapsedMs() const;       // 已播放毫秒(按样本数累计, 暂停自然冻结)

    // VU电平: 返回本窗口内L/R最大|样本|并清零(仅音频任务调用)
    void takeVU(uint16_t &l, uint16_t &r) {
        l = _vuMaxL; r = _vuMaxR;
        _vuMaxL = 0; _vuMaxR = 0;
    }

    // 协作让出模式(全格式启用): ESP8266Audio 2.x所有解码器的loop()都是do-while结构,
    // 依赖ConsumeSample返回false来让出主循环(返回false的样本库会保留, 下次重发, 无损)。
    // 每批128样本写满后让出一次: 外层主循环得以处理命令/快照/心跳。
    void setYieldEnabled(bool en) { yieldEnabled = en; yieldPending = false; }

    // 波形/频谱环形缓冲(仅音频任务写入, UI任务只读快照; 立体声1024帧≈23ms@44.1k)
    static constexpr size_t RING_FRAMES = 1024;
    void pushWaveSample(int16_t l, int16_t r);
    bool copyMono(int16_t* out, size_t len) const;
    bool copyWave(int16_t* outL, int16_t* outR, size_t len) const;
    friend class AudioPlayer;

private:
    void flushBuf();

    static const int BUF_SAMPLES = 128;
    I2SClass *_i2s;
    int16_t _buf[BUF_SAMPLES][2];
    int _count;
    unsigned long _rampStart;            // 起播软启动时间戳
    uint32_t _baseMs;                    // 采样率变化前累计的毫秒数
    uint32_t _samples;                   // 当前采样率下的样本计数
    uint16_t _curRate;                   // 当前采样率
    uint16_t _vuMaxL = 0;                // VU窗口内L最大|样本|
    uint16_t _vuMaxR = 0;                // VU窗口内R最大|样本|
    bool yieldEnabled = false;           // 协作让出开关
    bool yieldPending = false;           // 待让出标志(本批写满后下次调用返回false)
    int16_t _ringL[RING_FRAMES] = {0};
    int16_t _ringR[RING_FRAMES] = {0};
    volatile uint16_t _ringHead = 0;     // 下一写入位置
    volatile uint16_t _ringCount = 0;    // 已写入有效帧数(≤RING_FRAMES)
};

// ================= 音频播放引擎(单任务) =================
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    // 初始化: 仅挂载TF卡 + I2C探测WM8960在线(不写寄存器/不启动I2S, 无噪音)
    AudioInitState init();
    AudioInitState getInitState() const { return initState; }
    bool isCardReady() const { return cardReady; }

    // TF卡文件系统访问(仅浏览页在播放空闲时使用)
    fs::FS& getFS() { return SD_MMC; }

    // 强制重新检测TF卡
    void resetCard() {
        if (cardReady) SD_MMC.end();
        cardReady = false;
        initState = AUDIO_INIT_NONE;
    }

    // ---- 命令接口(UI任务调用, 全部非阻塞: 入队即返回) ----
    bool loadPlaylist(const std::vector<String>& files, int startIndex);  // 换列表并起播
    void playIndex(int index);            // 切到指定曲目(停旧开新)
    void stop();                          // 停止并断电
    void pause();
    void resume();

    // ---- 提示音(原蜂鸣器功能, 由WM8960驱动喇叭播放) ----
    // 非阻塞: 入队CMD_BEEP后立即返回; 仅空闲(IDLE)状态播放, 播放/暂停/录音中忽略
    bool beep(uint16_t durationMs = 100);

    // ---- 录音(与播放互斥; UI任务调用, 全部非阻塞入队) ----
    bool armRecording();                     // 开录音路径进入监听(电平表活动, 不写文件)
    bool startRecording(const String& path); // 建WAV文件开始写入(未监听则自动先开路径)
    void pauseRecording();                   // 暂停写入(继续监听)
    void resumeRecording();                  // 继续写入
    void stopRecording();                    // 停止: 剩余落盘+回写WAV头+关路径
    void setRecGain(int db);                 // 录音增益(ADC数字音量, 见REC_GAIN_*)
    int getRecGainDb() const { return recGainDb; }
    void setRecSource(int src);              // 切换输入源(仅待机时调用, 监听即时生效)
    int getRecSource() const { return recSourceV; }

    // ---- 对讲通话(与播放/录音互斥; UI任务调用, 全部非阻塞入队) ----
    bool startTalk();                        // 开TALK双工引擎(开路径+进talkLoop)
    void stopTalk();                         // 停TALK引擎(关路径, 收尾回主循环)
    void setTalkPtt(bool talking);           // PTT按下态(UI写, 音频任务读)
    void setTalkMuted(bool muted);           // 本端静音(UI写, 音频任务读)
    void setTalkMode(int mode);              // 0 PTT / 1 全双工(UI写, 音频任务读)
    void setTalkEchoSuppress(bool en) { talkEchoSuppressV = en; }  // 全双工回声门开关
    TalkState getTalkState() const { return (TalkState)talkStateV; }

    // ---- 参数 ----
    void setVolume(int vol);              // 入队(音频任务直接I2C写, 立即生效)
    int getVolume() const { return targetVolume; }
    void setPlayMode(PlayMode mode) { playModeV = (int)mode; }
    PlayMode getPlayMode() const { return (PlayMode)playModeV; }
    bool isPathOpen() const { return audioPathOpen; }  // 录音/回放路径状态(供跨路径软关断判断)

    // ---- 喇叭输出开关(音乐页按键3, 诊断Class-D耦合噪声/仅耳机使用偏好) ----
    void setSpeakerOn(bool on);           // 入队, 音频任务切换功放电源位
    bool isSpeakerOn() const { return speakerOn; }

    // ---- 状态快照(任意任务免锁只读) ----
    PlayerState getState() const { return state; }
    bool isPlaying() const { return state == PLAYER_PLAYING; }
    bool isPaused() const { return state == PLAYER_PAUSED; }
    int getIndex() const { return curIndex; }              // 当前曲目索引(-1=无)
    bool isStartFailed() const { return startFailed; }     // 列表全部启动失败
    uint32_t getElapsedMs() const { return snapElapsed; }
    uint32_t getTotalMs() const { return snapTotal; }
    uint32_t getFilePos() const { return snapPos; }
    uint32_t getFileSize() const { return snapSize; }
    uint32_t getSampleRate() const { return snapRate; }
    uint16_t getBitDepth() const { return snapBits; }     // 位深(文件原生, 未解析到默认16)

    // VU电平快照(0..100, 音频任务50ms窗口更新; 录音期间由录音引擎更新, 播放录音互斥复用)
    uint8_t getVuL() const { return snapVuL; }
    uint8_t getVuR() const { return snapVuR; }
    uint8_t getPeakL() const { return snapPeakL; }
    uint8_t getPeakR() const { return snapPeakR; }
    uint8_t getSpkLevel() const { return snapSpk; }   // 对讲RX喇叭电平(0..100, 无远端语音时衰减到0)

    // 频谱/波形快照(任意任务可调用, 无锁快照拷贝; len≤1024)
    bool getSpectrumSamples(int16_t* out, size_t len) const;
    bool getWaveformSamples(int16_t* outL, int16_t* outR, size_t len) const;

    // 录音快照(任意任务免锁只读)
    RecState getRecState() const { return (RecState)recStateV; }
    uint32_t getRecElapsedMs() const { return snapRecElapsed; }   // 已录毫秒(暂停冻结)
    uint32_t getRecFileSize() const { return snapRecBytes; }      // WAV文件当前大小(含44字节头)
    bool getRecClipL() const { return snapClipL; }                // 左声道削波(爆VU)标志
    bool getRecClipR() const { return snapClipR; }                // 右声道削波(爆VU)标志
    bool getRecError() const { return recError; }                 // 录音失败(建文件/写卡错误)

    static bool isAudioFile(const String& name);
    static const char* MOUNT_POINT;

    // I2C总线互斥(WM8960与OLED渲染共享总线)
    SemaphoreHandle_t i2cMutex = nullptr;

    void codecI2cLock();                          // 取i2cMutex(与OLED共享总线互斥, AI采集复用)
    void codecI2cUnlock();                        // 释放i2cMutex

private:
    // ---- 命令(队列传递) ----
    enum PlayerCmd {
        CMD_NONE = 0, CMD_PLAY, CMD_STOP, CMD_PAUSE, CMD_RESUME, CMD_VOL, CMD_LOAD, CMD_BEEP,
        CMD_REC_ARM,     // 开录音路径进入监听
        CMD_REC_START,   // 建WAV文件开始写入
        CMD_REC_PAUSE,   // 暂停写入
        CMD_REC_RESUME,  // 继续写入
        CMD_REC_STOP,    // 停止(收尾后回主循环)
        CMD_REC_VOL,     // 写录音增益(ADC数字音量)
        CMD_REC_SRC,     // 切换输入源(msg.index=RecSource)
        CMD_SPK,          // 喇叭功放开关(诊断噪声/仅耳机模式)
        CMD_TALK_START,   // 开TALK双工引擎
        CMD_TALK_STOP,    // 停TALK引擎
        CMD_TALK_PTT,     // PTT按下态(msg.index=0/1)
        CMD_TALK_MUTE,    // 本端静音(msg.index=0/1)
        CMD_TALK_MODE     // 通话模式(msg.index=0 PTT/1全双工)
    };
    struct AudioCmdMsg {
        uint8_t cmd;
        int index;
        const std::vector<String>* list;  // 仅CMD_LOAD使用
        const String* path;               // 仅CMD_REC_START使用(页面成员, 存活期覆盖命令处理)
    };

    // 音频任务(唯一任务: 命令处理+解码推进)
    void audioTaskLoop();
    void handleCommand(const AudioCmdMsg& msg);   // 同步执行(demo同款串行流程)
    void advanceOnEnded();                        // 曲目播完: 按循环模式前进
    bool startTrack(int index);                   // 带逐曲重试的启动
    bool tryStart(int index);                     // 单曲: 停旧+开文件+起新解码器
    void stopDecoder();                           // decoder->stop()+delete(demo官方方法, 无泄漏)
    void fullStop();                              // 停解码器+关音频路径(断电)
    bool openAudioPath();                         // I2S + WM8960播放配置
    void closeAudioPath();                        // WM8960软关断 + I2S关闭(防爆音, 禁reset硬复位)
    void primeSilentFrames();                     // 起播前预送静音帧, 让DMA/时钟跑稳再解mute
    bool applyVolumeLocked();                     // 已持i2cMutex时写音量(播放中调音量, 一步到位)
    bool rampVolumeForTargetLocked();             // 已持i2cMutex时音量软斜坡(上电/开路用, 防跳变)
    static float spkVolumeDb(int vol);            // 音量0..100 -> 喇叭dB(封顶0dB)
    void playBeepTone(int durationMs);            // 滴声: 正弦波短音(仅音频任务, 开路->播->关路)
    void updateSnapshot();                        // 刷新快照供UI读取

    // ---- 录音引擎(仅音频任务; 配置与USBTFCard参考工程已验证的录音通路一致) ----
    bool openRecordPath();                        // I2S(全双工) + WM8960麦克风输入配置
    void closeRecordPath();                       // WM8960复位断电 + I2S关闭
    bool applyRecGainLocked();                    // 已持i2cMutex时按源写左右ADC数字音量
    bool beginRecFile(const String& path);        // 建WAV文件并写占位头(声道数按源定)
    void finishRecFile();                         // 回写真实长度并关闭文件
    void flushRecData();                          // 追赶式落盘: 写SD间隙穿插读DMA防溢出
    void recProcessChunk(uint8_t* data, size_t& len);  // 按源处理DMA块: 单声道原地打包+VU/削波
    void recordLoop();                            // 录音主循环(命令/读取/VU/落盘, 含监听与暂停态)

    // ---- 对讲引擎(仅音频任务; 复用录音输入+播放输出, 44.1k硬件时钟+软件重采样) ----
    bool openTalkPath();                          // 开44.1k全双工路(播放+录音同构, speakerOn决定喇叭)
    void closeTalkPath();                         // 软关断(复用closeAudioPath+closeRecordPath语义)
    void talkLoop();                              // 对讲主循环(命令/DMA读/ADPCM/ESP-NOW/DMA写/VU)
    void talkProcessMic(const int16_t* stereo, int frames);  // MIC→重采样→ADPCM→TX队列
    void talkProcessSpk(int16_t* stereo, int frames);        // RX队列→ADPCM解码→重采样→混入播放帧
    bool mountSD();
    AudioGenerator* createDecoder(const String& lowerExt) const;
    uint32_t estimateTotalMs(const String& path, const String& lowerExt,
                             uint32_t* outRate, uint16_t* outBits) const;
    static int vuToPct(uint16_t amp);             // 幅值->0..100 (dB刻度-60..0dB)
    static void postCmd(QueueHandle_t q, AudioCmdMsg& msg, const char* name);  // 入队(失败打印)

    static void audioTaskFunc(void* param);
    static const int TASK_STACK = 16384;

    // ---- 初始化状态 ----
    AudioInitState initState = AUDIO_INIT_NONE;
    bool cardReady = false;
    bool codecChecked = false;
    bool codecPresent = false;
    bool audioPathOpen = false;
    bool taskStarted = false;

    I2SClass i2s;
    WM8960 codec;
    AudioOutputWM8960 *output = nullptr;
    AudioGenerator *decoder = nullptr;      // 仅音频任务访问
    AudioFileSourceFS *audioFile = nullptr; // 仅音频任务访问
    std::vector<String> playlist;           // 播放列表(音频任务在停机点替换)

    // ---- 通信 ----
    QueueHandle_t cmdQueue = nullptr;       // UI→音频任务命令队列

    // ---- 参数与快照(volatile跨任务) ----
    volatile int targetVolume = 50;
    volatile bool speakerOn = true;         // 喇叭功放开关(默认开; 关=仅耳机, 诊断Class-D噪声)
    volatile int playModeV = PLAYMODE_SEQUENCE;
    volatile PlayerState state = PLAYER_IDLE;
    volatile int curIndex = -1;
    volatile bool startFailed = false;
    volatile uint32_t snapElapsed = 0;
    volatile uint32_t snapTotal = 0;
    volatile uint32_t snapPos = 0;
    volatile uint32_t snapSize = 0;
    volatile uint32_t snapRate = 0;
    volatile uint16_t snapBits = 0;          // 位深(文件原生)
    volatile uint8_t snapVuL = 0;            // VU电平快照0..100
    volatile uint8_t snapVuR = 0;
    volatile uint8_t snapPeakL = 0;          // 峰值保持(慢衰减)
    volatile uint8_t snapPeakR = 0;
    volatile uint8_t snapSpk = 0;            // 对讲RX喇叭电平快照0..100(远端语音, 无声衰减到0)
    unsigned long lastVuAt = 0;              // 音频任务内VU采样节流

    // ---- 录音(状态/参数为volatile跨任务; 文件与缓冲仅音频任务访问) ----
    volatile int recStateV = REC_IDLE;       // 录音状态机
    volatile int recGainDb = REC_GAIN_DEFAULT;   // 用户增益(ADC数字音量dB)
    volatile int recSourceV = REC_SRC_DEFAULT;   // 输入源(见RecSource, UI写/音频任务读)
    volatile bool recError = false;          // 录音失败标志(建文件/写卡错误)
    volatile uint32_t snapRecElapsed = 0;    // 已录毫秒快照
    volatile uint32_t snapRecBytes = 0;      // WAV文件大小快照
    volatile bool snapClipL = false;         // 削波(爆VU)标志, 命中1.5秒后自动清
    volatile bool snapClipR = false;
    bool recPathOpen = false;                // 录音路径(I2S+WM8960输入)已开
    File recFile;                            // WAV文件句柄
    bool recFileOpen = false;
    String recPath;                          // 当前录音文件完整路径
    uint8_t* recBuf = nullptr;               // 落盘缓冲(录音期间分配, 停止后释放)
    uint32_t recBufPos = 0;                  // 缓冲内待写SD字节数
    uint32_t recWritten = 0;                 // 已写SD的data字节数
    uint32_t recFrames = 0;                  // 已录采样帧数(时长=帧数/采样率, mono帧=2B/stereo帧=4B)
    uint8_t recChannels = 2;                 // WAV声道数(按源: 内置/耳机=1, 双麦=2)
    uint16_t recFrameBytes = 4;              // 每采样帧字节数(=声道数*2)
    uint16_t recVuMaxL = 0;                  // 50ms窗口内L/R最大|样本|
    uint16_t recVuMaxR = 0;
    bool recClipHitL = false;                // 本窗口内是否出现削波样本
    bool recClipHitR = false;
    unsigned long clipUntilL = 0;            // 削波标志保持时限
    unsigned long clipUntilR = 0;
    unsigned long recMuteUntil = 0;          // 起录/恢复静音期(丢弃ADC上电瞬态与旧数据)
    uint32_t recOverflow = 0;                // 缓冲满丢样本计数(诊断, 正常恒为0)
    unsigned long lastRecBeatMs = 0;         // 录音心跳节流

    // 录音缓冲参数: 64KB线性缓冲, 攒满一半穿插式落盘(SD写速率>音频产生速率
    // 单声道88.2KB/s / 立体声176.4KB/s, 追赶式写入期间每写4KB回读一次DMA防溢出)
    static constexpr uint32_t REC_BUF_SIZE = 64 * 1024;    // 落盘缓冲总大小
    static constexpr uint16_t REC_READ_STEP = 2048;        // 单次DMA读取字节(11.6ms音频)
    static constexpr uint32_t REC_BUF_FLUSH = 32 * 1024;   // 触发落盘阈值
    static constexpr uint16_t REC_SD_CHUNK = 4096;         // 单次写SD字节(减小写卡电流冲击)
    static constexpr uint16_t REC_CLIP_TH = 32600;         // 削波判定阈值(约-0.04dBFS)
    uint8_t recChunk[REC_READ_STEP];         // DMA读取暂存(监听/暂停态读后即弃)
    // (已删除占位字段)

    // ---- 对讲(状态/参数为volatile跨任务; 编解码state与缓冲仅音频任务访问) ----
    // 时钟策略: 硬件I2S恒跑44.1k(已验证通路不动), 软件重采样到8k/16k做ADPCM。
    // PTT=16k帧(384样本@16k=24ms, 约64kbps); 全双工=8k帧(384样本@8k=48ms, 约32kbps)。
    volatile int talkStateV = TALK_IDLE;
    volatile bool talkPttV = false;          // PTT按下态
    volatile bool talkMutedV = false;        // 本端静音(不编码发送)
    volatile int talkModeV = 0;              // 0 PTT / 1 全双工
    volatile bool talkEchoSuppressV = true;  // 全双工回声门开关(默认开, UI 6键可切换)
    bool talkPathOpen = false;
    // 重采样中间缓冲(44.1k 512帧立体声DMA块 → 单声道 → 目标率)
    int16_t talkRsBuf[TALK_VOICE_SAMPLES * 3];  // 重采样输出(16k 24ms=384; 48ms 8k=384, 余量3倍)
    int16_t talkPcmBuf[TALK_VOICE_SAMPLES];  // ADPCM编解码PCM帧
    int16_t talkSpkBuf[TALK_VOICE_SAMPLES * 6]; // 解码后上采样到44.1k(48ms 8k→2116, 余量6倍)
    uint8_t talkAdpcmBuf[TALK_VOICE_FRAME_BYTES];
    // ADPCM连续流状态(跨帧保留; PTT按下沿/模式切换重置)
    int talkEncPred = 0, talkEncIdx = 0;
    int talkDecPred = 0, talkDecIdx = 0;
    int talkRsCount = 0;                 // 重采样攒帧计数(仅音频任务访问)
    // 累积器(44.1k→目标率分数步进): acc+=target, 每满44100取一帧源样本
    uint32_t talkRsAcc = 0;
    int16_t talkRsLast = 0;                  // 上一源样本(线性插值用)
    // 上采样累积(目标率→44.1k)
    uint32_t talkUsAcc = 0;
    int talkUsPos = 0, talkUsLen = 0;        // 当前解码帧消费位置
    int16_t talkUsLast = 0;
    uint16_t talkRxMax = 0;                  // 50ms窗口内RX解码最大|样本|(喇叭电平用)
    unsigned long lastTalkBeatMs = 0;

    TaskHandle_t taskHandle = nullptr;
};

extern AudioPlayer audioPlayer;

// ================= 声音提示辅助(全局) =================
// 原蜂鸣器接口的等价替代: 受"声音提示"系统设置控制,
// 由WM8960驱动喇叭播放约2.7kHz正弦"滴"声(任意任务可调用, 非阻塞)
void SoundAlertBeep(uint16_t durationMs = 100);

#endif
