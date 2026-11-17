/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#include "RadioHelper.h"
#include "../Bsp/IOPin.h"
#include "AudioPlayer.h"
#include "../Services/SystemSetting.h"
#include "../Services/RawSignalFiles.h"
#include "../Util/TaskConfig.h"
#include <driver/gpio.h>
#include <LittleFS.h>
#include "../Util/Log.h"
#include "../Lib/RFProtocolCameStyle.h"
#include "../Lib/RFSubGhzPresets.h"
#include "../Lib/RawCaptureSession.h"
#include "../Lib/RawSubGhzReader.h"
#include "../Lib/RFTransmitConfig.h"

#define LOG_TAG "RADIO"

extern DataStore dataStore;

// 同步发射与RAW worker共用所有权，避免Web/主页同时改写CC1101和静态波形缓冲。
class RadioTransmitGuard {
public:
    explicit RadioTransmitGuard(std::atomic<bool>& busy) : busy(busy), acquired(!busy.exchange(true)) {}
    ~RadioTransmitGuard() { if (acquired) busy.store(false); }
    explicit operator bool() const { return acquired; }
private:
    std::atomic<bool>& busy;
    bool acquired;
};

// ==================== CameStyle 协议辅助 ====================
// 按协议名匹配 CameStyle 协议常量（CAME / NICE FLO / HOLTEK），
// 未命中返回 nullptr（走原 transmitPT2262 路径）
static const CameStyleProtoConst* findCameStyleConst(const char* name) {
    if (name == nullptr || name[0] == '\0') return nullptr;
    if (strcmp(name, PROTO_CAME.name) == 0)     return &PROTO_CAME;
    if (strcmp(name, PROTO_NICE_FLO.name) == 0)  return &PROTO_NICE_FLO;
    if (strcmp(name, PROTO_HOLTEK.name) == 0)    return &PROTO_HOLTEK;
    return nullptr;
}

// ==================== 扫描频率表定义（MHz） ====================
// 低频段：部分美国/日本车库门、旧式门铃
const float RF_FREQ_TABLE_LOW[] = {
    310.0,
    315.0,
    330.0,
    350.0
};
const int RF_FREQ_COUNT_LOW = sizeof(RF_FREQ_TABLE_LOW) / sizeof(RF_FREQ_TABLE_LOW[0]);

// 高频段：北美/英国/欧洲/中国车库门与遥控器（最常用频段）
const float RF_FREQ_TABLE_HIGH[] = {
    390.0,
    418.0,
    430.5,
    431.5,
    432.8,
    433.92
};
const int RF_FREQ_COUNT_HIGH = sizeof(RF_FREQ_TABLE_HIGH) / sizeof(RF_FREQ_TABLE_HIGH[0]);

// 全部频点（低频在前高频在后，编辑页面频率选择用）
const float RF_FREQ_TABLE_ALL[] = {
    310.0, 315.0, 330.0, 350.0,
    390.0, 418.0, 430.5, 431.5, 432.8, 433.92
};
const int RF_FREQ_COUNT_ALL = sizeof(RF_FREQ_TABLE_ALL) / sizeof(RF_FREQ_TABLE_ALL[0]);

// 低频/高频分界（MHz）：≤360 视为低频段
#define RF_BAND_SPLIT_FREQ  360.0f

RFBand RFGetBandByFrequency(float freqMHz) {
    return (freqMHz <= RF_BAND_SPLIT_FREQ) ? RF_BAND_LOW : RF_BAND_HIGH;
}

// 声音提示全局入口（WM8960滴声, 见AudioPlayer.h）
extern SystemSetting systemSetting;

RadioHelper::RadioHelper() :
    bReceiveMode(false),
    bRawMode(false),
    cc1101Ready(false),
    currentBand(RF_BAND_HIGH),
    fixedFreqMHz(0.0f),
    fixedFreqMode(false),
    repeatCount(15),
    txPowerLevel(7),
    hasNewSignal(false),
    receiveMutex(nullptr),
    receiveStartSemaphore(nullptr),
    radioReceiveTaskHandle(nullptr),
    scanner(cc1101, decoder)   // FrequencyScanner无默认构造, 须带驱动/解码器引用
{
}

RadioHelper::~RadioHelper()
{
    if (receiveMutex != nullptr) {
        vSemaphoreDelete(receiveMutex);
        receiveMutex = nullptr;
    }
}

void RadioHelper::init()
{
    LOG_I("RadioHelper::init (CC1101)");

    // 创建互斥锁
    if (receiveMutex == nullptr) {
        receiveMutex = xSemaphoreCreateMutex();
    }

    // 初始化CC1101射频芯片
    cc1101Ready = initCC1101();
    if (!cc1101Ready) {
        LOG_E("ERROR: CC1101 init failed!");
    }

    // 初始化解码器（GDO2中断采集）
    decoder.begin(PIN_CC1101_GDO0, PIN_CC1101_GDO2);

    // 从SystemSetting读取重复发送次数配置
    int repeatTransmit = systemSetting.getRepeatTransmit();
    if (repeatTransmit > 0 && repeatTransmit <= 255) {
        SetRepeatTransmit(repeatTransmit);
        LOG_I("RadioHelper: 重复发送次数: %d", repeatTransmit);
    } else {
        SetRepeatTransmit(15);
        LOG_I("RadioHelper: 使用默认重复发送次数: 15");
    }

    // 发射功率档（0-7，默认7=最大+10dBm）
    SetTxPowerLevel(systemSetting.getTxPowerLevel());

    // 接收灵敏度档应用到 AGC（见 applyRxSensitivity）
    applyRxSensitivity(systemSetting.getRxSensitivity());

    // 详细脉冲打印（联调用完改回 false，避免串口刷屏）
    decoder.setVerboseDebug(false); // 避免串口长时间阻塞 RX 解码任务

    // 创建信号量，用于触发接收任务开始接收
    receiveStartSemaphore = xSemaphoreCreateBinary();

    // 创建接收任务（栈12KB：available()内1.5KB脉冲拷贝 +
    // decodeSignal→analyzeTiming内1.6KB直方图 + printf浮点格式化开销，
    // 6KB在噪声多、频繁走fallback解码时触发栈看门狗复位）
    // 钉Core1(业务核): 与GDO2采集ISR同核, 优先级3=硬时序业务(规划§4.3)
    xTaskCreatePinnedToCore(
        radioReceiveTask,
        TASK_RADIO_NAME,
        12288,                      // 12KB栈(实测校准)
        this,
        TASK_PRIO_HW,
        &radioReceiveTaskHandle,
        TASK_CORE_APP
    );
}

// ──────────────────────────────────────────────────────────────────────────────
// CC1101初始化：对齐 Flipper OOK 270kHz 异步预设
// （flipperzero-firmware-dev/lib/subghz/devices/cc1101_configs.c）
// ──────────────────────────────────────────────────────────────────────────────
bool RadioHelper::initCC1101()
{
    LOG_I("[INIT] CC1101...");

    if (!cc1101.begin(PIN_CC1101_SCK, PIN_CC1101_MISO, PIN_CC1101_MOSI,
                      PIN_CC1101_CS, PIN_CC1101_GDO0, PIN_CC1101_GDO2)) {
        return false;
    }

    setAntennaBand(RF_BAND_HIGH);
    bool configured = configureReceive(433.92f, RF_PRESET_AM270, true);
    LOG_I("[INIT] CC1101 configured=%d SCK=%d SO=%d SI=%d CS=%d TX=%d RX=%d SW=%d",
          configured, PIN_CC1101_SCK, PIN_CC1101_MISO, PIN_CC1101_MOSI,
          PIN_CC1101_CS, PIN_CC1101_GDO0, PIN_CC1101_GDO2, PIN_RF_SW_CTRL);
    return configured;
}

// RAW / 普通扫描 / 单频接收都从同一复位基线加载预设，不继承 TX 或 FM 设置。
bool RadioHelper::configureReceive(float freqMHz, uint8_t presetId, bool sensitivity) {
    decoder.enableRawMode(false);
    if (!cc1101.reset() || !cc1101.checkConnection()) return false;
    const RFSubGhzPreset& preset = RfPreset(presetId);
    if (!cc1101.loadRegisterPairs(preset.regPairs, false)) return false;

    // GPIO39 由预设配置为异步数据，GPIO40 提供既有协议解码的并行边沿。
    cc1101.setGDO2Output(0x0D);
    cc1101.writeReg(CC1101_IOCFG1, 0x2E);
    cc1101.writeReg(CC1101_SYNC1, 0x00);
    cc1101.writeReg(CC1101_SYNC0, 0x00);
    cc1101.writeReg(CC1101_PKTCTRL1, 0x04);
    cc1101.writeReg(CC1101_MCSM1, 0x30);
    cc1101.writeReg(CC1101_MCSM2, 0x07);
    cc1101.writeReg(CC1101_CHANNR, 0x00);
    cc1101.writeReg(CC1101_FSCTRL0, 0x00);

    bool configured = cc1101.setFrequency((uint32_t)(freqMHz * 1000000.0f));
    for (const uint8_t* pair = preset.regPairs; pair[0] || pair[1]; pair += 2) {
        uint8_t actual = cc1101.readReg(pair[0]);
        if (actual != pair[1]) {
            LOG_E("RX register 0x%02X: expected=0x%02X actual=0x%02X",
                  pair[0], pair[1], actual);
            configured = false;
        }
    }
    const uint8_t required[][2] = {
        {CC1101_IOCFG2, 0x0D}, {CC1101_IOCFG1, 0x2E},
        {CC1101_PKTCTRL1, 0x04}, {CC1101_MCSM1, 0x30},
        {CC1101_MCSM2, 0x07}, {CC1101_CHANNR, 0}, {CC1101_FSCTRL0, 0},
        {CC1101_SYNC1, 0}, {CC1101_SYNC0, 0}
    };
    for (const auto& pair : required) {
        uint8_t actual = cc1101.readReg(pair[0]);
        if (actual != pair[1]) {
            LOG_E("RX register 0x%02X: expected=0x%02X actual=0x%02X",
                  pair[0], pair[1], actual);
            configured = false;
        }
    }
    if (!configured) { cc1101.setIdleMode(); return false; }
    if (sensitivity) applyRxSensitivity(systemSetting.getRxSensitivity());
    // TI DN022：带宽跨过 325kHz 时，FIFO/模拟测试配置也必须一同切换。
    // 显式写入并回读，避免复位默认值或上一调制模式留下不匹配的基线。
    uint8_t mdm4 = cc1101.readReg(CC1101_MDMCFG4);
    uint32_t bandwidth = 26000000UL / (8UL * (4 + ((mdm4 >> 4) & 3)) * (1UL << (mdm4 >> 6)));
    bool wide = bandwidth > 325000;
    // AM650 的默认组合沿用 RX-DUAL-8，与频率无关。
    const bool verifiedOok = presetId == RF_PRESET_AM650 && !sensitivity;
    const uint8_t tuning[][2] = {
        {CC1101_FIFOTHR, (uint8_t)(wide ? 0x07 : 0x47)},
        {CC1101_TEST2, (uint8_t)(wide && !verifiedOok ? 0x88 : 0x81)},
        {CC1101_TEST1, (uint8_t)(wide && !verifiedOok ? 0x31 : 0x35)}, {CC1101_TEST0, 0x09}
    };
    for (const auto& pair : tuning) cc1101.writeReg(pair[0], pair[1]);
    for (const auto& pair : tuning) {
        if (cc1101.readReg(pair[0]) != pair[1]) {
            LOG_E("RX bandwidth tuning 0x%02X failed", pair[0]);
            cc1101.setIdleMode(); return false;
        }
    }
    // pinMode(INPUT) 释放上次 RMT TX 对 GPIO39 的占用，重新绑定 GPIO40 ISR。
    decoder.begin(PIN_CC1101_GDO0, PIN_CC1101_GDO2);
    decoder.reset();
    cc1101.flushRx();
    return true;
}

void RadioHelper::logReceiveState(const char* mode) {
    LOG_I("[RX.check] %s ANT_GPIO%d=%d IOCFG0/2=%02X/%02X PKTCTRL0=%02X "
          "MDMCFG2/3/4=%02X/%02X/%02X AGC2/1/0=%02X/%02X/%02X "
          "FIFO=%02X TEST2/1=%02X/%02X FREQ=%02X%02X%02X MARC=%02X RSSI=%ddBm GDO0/2=%d/%d",
          mode, PIN_RF_SW_CTRL, digitalRead(PIN_RF_SW_CTRL),
          cc1101.readReg(CC1101_IOCFG0), cc1101.readReg(CC1101_IOCFG2),
          cc1101.readReg(CC1101_PKTCTRL0), cc1101.readReg(CC1101_MDMCFG2),
          cc1101.readReg(CC1101_MDMCFG3), cc1101.readReg(CC1101_MDMCFG4),
          cc1101.readReg(CC1101_AGCCTRL2), cc1101.readReg(CC1101_AGCCTRL1),
          cc1101.readReg(CC1101_AGCCTRL0),
          cc1101.readReg(CC1101_FIFOTHR), cc1101.readReg(CC1101_TEST2),
          cc1101.readReg(CC1101_TEST1),
          cc1101.readReg(CC1101_FREQ2), cc1101.readReg(CC1101_FREQ1),
          cc1101.readReg(CC1101_FREQ0), cc1101.getMARCState(), cc1101.getRSSIdBm(),
          digitalRead(PIN_CC1101_GDO0), digitalRead(PIN_CC1101_GDO2));
}

// ──────────────────────────────────────────────────────────────────────────────
// 天线开关控制：低频段=LOW，高频段=HIGH（HIGH选择高频通路）
// ──────────────────────────────────────────────────────────────────────────────
void RadioHelper::setAntennaBand(RFBand band)
{
    pinMode(PIN_RF_SW_CTRL, OUTPUT);
    digitalWrite(PIN_RF_SW_CTRL, (band == RF_BAND_LOW) ? LOW : HIGH);
    delayMicroseconds(100); // 切换完成后再校准/收发
    LOG_I("[ANT] GPIO%d=%d band=%s", PIN_RF_SW_CTRL, digitalRead(PIN_RF_SW_CTRL),
          band == RF_BAND_LOW ? "LOW" : "HIGH");
}

RFBand RadioHelper::bandOfFrequency(float freqMHz)
{
    return RFGetBandByFrequency(freqMHz);
}

// ──────────────────────────────────────────────────────────────────────────────
// 接收控制
// ──────────────────────────────────────────────────────────────────────────────
void RadioHelper::EnableReceive(RFBand band)
{
    if (IsTransmitting()) return;
    LOG_I("EnableReceive band=%s", band == RF_BAND_LOW ? "LOW(310-350)" : "HIGH(390-434)");

    if (receiveMutex != nullptr) {
        xSemaphoreTake(receiveMutex, portMAX_DELAY);
    }
    if (IsTransmitting()) {
        if (receiveMutex) xSemaphoreGive(receiveMutex);
        return;
    }

    // 如果已在接收模式，先停止
    if (bReceiveMode) {
        scanner.stop();
        bReceiveMode = false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // 清空之前的数据
    memset(&rcData, 0, sizeof(rcData));
    hasNewSignal = false;

    if (!cc1101Ready) {
        LOG_E("EnableReceive: CC1101未就绪!");
        cc1101Ready = initCC1101();
    }
    if (!cc1101Ready) {
        xSemaphoreGive(receiveMutex);
        return;
    }

    // 设置天线开关到对应频段
    currentBand = band;
    fixedFreqMode = false;
    setAntennaBand(band);

    bRawMode = false;
    const float* frequencies = band == RF_BAND_LOW ? RF_FREQ_TABLE_LOW : RF_FREQ_TABLE_HIGH;
    const int count = band == RF_BAND_LOW ? RF_FREQ_COUNT_LOW : RF_FREQ_COUNT_HIGH;
    bReceiveMode = beginOokReceive(frequencies, count);

    logReceiveState(fixedFreqMode ? "FIXED" : "SCAN");
    xSemaphoreGive(receiveMutex);

    // 通知接收任务开始工作（先清空再给信号量，避免累积）
    if (receiveStartSemaphore != nullptr) {
        xSemaphoreTake(receiveStartSemaphore, 0);
        xSemaphoreGive(receiveStartSemaphore);
    }
}

void RadioHelper::EnableReceiveFixed(float freqMHz)
{
    if (IsTransmitting()) return;
    if (freqMHz < 300.0f || freqMHz > 460.0f) {
        LOG_E("EnableReceiveFixed: frequency out of range %.2fMHz", freqMHz);
        return;
    }
    LOG_I("EnableReceiveFixed freq=%.2fMHz", freqMHz);

    if (receiveMutex != nullptr) {
        xSemaphoreTake(receiveMutex, portMAX_DELAY);
    }
    if (IsTransmitting()) {
        if (receiveMutex) xSemaphoreGive(receiveMutex);
        return;
    }

    // 如果已在接收模式，先停止
    if (bReceiveMode) {
        scanner.stop();
        bReceiveMode = false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // 清空之前的数据
    memset(&rcData, 0, sizeof(rcData));
    hasNewSignal = false;

    if (!cc1101Ready) {
        LOG_E("EnableReceiveFixed: CC1101未就绪!");
        cc1101Ready = initCC1101();
    }
    if (!cc1101Ready) {
        xSemaphoreGive(receiveMutex);
        return;
    }

    // 天线开关按频率自动切换（PIN_RF_SW_CTRL：低频段=LOW，高频段=HIGH）
    RFBand band = bandOfFrequency(freqMHz);
    currentBand = band;
    setAntennaBand(band);

    // 单频点守候：扫描表只有一个频点，scanner 不跳频
    fixedFreqMHz = freqMHz;
    fixedFreqMode = true;
    bRawMode = false;
    fixedRxSessionAt = millis();
    if (!beginOokReceive(&fixedFreqMHz, 1)) {
        LOG_E("EnableReceiveFixed: RX configuration failed");
        xSemaphoreGive(receiveMutex);
        return;
    }
    bReceiveMode = true;

    logReceiveState(fixedFreqMode ? "FIXED" : "SCAN");
    xSemaphoreGive(receiveMutex);

    // 通知接收任务开始工作（先清空再给信号量，避免累积）
    if (receiveStartSemaphore != nullptr) {
        xSemaphoreTake(receiveStartSemaphore, 0);
        xSemaphoreGive(receiveStartSemaphore);
    }
}

bool RadioHelper::beginOokReceive(const float* frequencies, int count) {
    if (!frequencies || count <= 0) return false;

    // 灵敏度按用户档位选射频参数。所有频率共享采集与解码器，监听中保持预设。
    const int sensitivity = systemSetting.getRxSensitivity();
    const uint8_t preset = sensitivity == 0 ? RF_PRESET_AM270 : RF_PRESET_AM650;
    if (!configureReceive(frequencies[0], preset, sensitivity == 2) ||
        !scanner.begin(frequencies, count)) {
        LOG_E("OOK RX setup failed");
        return false;
    }
    decoder.beginOokCapture(PIN_CC1101_GDO0);
    LOG_I("[RX.profile] %s preset=%s sensitivity=%d",
          count == 1 ? "FIXED" : "SCAN", RfPreset(preset).displayName, sensitivity);
    return true;
}

void RadioHelper::DisableReceive()
{
    LOG_I("DisableReceive");

    if (receiveMutex != nullptr) {
        xSemaphoreTake(receiveMutex, portMAX_DELAY);
    }

    if (bReceiveMode) {
        bReceiveMode = false;
        scanner.stop();  // CC1101进IDLE，清空缓冲
    }

    if (receiveMutex != nullptr) {
        xSemaphoreGive(receiveMutex);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// RAW 连续录制接收（频点+调制预设直配，解码器切 RAW 环形模式）
// ──────────────────────────────────────────────────────────────────────────────
bool RadioHelper::EnableRawReceive(float freqMHz, uint8_t presetId) {
    if (IsTransmitting()) return false;
    if (!cc1101Ready) {
        LOG_E("EnableRawReceive: CC1101未就绪!");
        return false;
    }
    if (freqMHz < 300.0f || freqMHz > 460.0f) {
        LOG_E("EnableRawReceive: 频率越界 %.2fMHz", freqMHz);
        return false;
    }

    if (receiveMutex != nullptr) xSemaphoreTake(receiveMutex, portMAX_DELAY);
    if (IsTransmitting()) {
        if (receiveMutex) xSemaphoreGive(receiveMutex);
        return false;
    }

    // 与扫描接收互斥
    if (bReceiveMode) {
        bReceiveMode = false;
        scanner.stop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    bRawMode = false;
    decoder.enableRawMode(false);
    cc1101.setIdleMode();
    setAntennaBand(bandOfFrequency(freqMHz));

    const RFSubGhzPreset& p = RfPreset(presetId);
    bool configured = configureReceive(freqMHz, presetId, false);
    bool rxReady = false;
    if (configured) {
        // RAW 记录实际的异步数据电平。GPIO39 与验证过的接收通路相同。
        decoder.begin(PIN_CC1101_GDO0, PIN_CC1101_GDO0);
        decoder.enableRawMode(true);
        rxReady = cc1101.setRxMode();
    }
    uint8_t marc = cc1101.getMARCState() & 0x1F;
    bRawMode = configured && rxReady;
    if (!bRawMode) {
        LOG_E("RAW RX failed: registers=%d MARCSTATE=0x%02X", configured, marc);
        cc1101.setIdleMode();
        decoder.enableRawMode(false);
        decoder.end();
        cc1101.restoreDefaultConfig();
        applyRxSensitivity(systemSetting.getRxSensitivity());
    }

    if (receiveMutex != nullptr) xSemaphoreGive(receiveMutex);

    logReceiveState("RAW");
    LOG_I("EnableRawReceive %.2fMHz preset=%s(%s) RX=%d MARC=0x%02X RSSI=%ddBm",
          freqMHz, p.displayName, p.isFM ? "FM" : "AM", bRawMode, marc,
          cc1101.getRSSIdBm());
    return bRawMode;
}

void RadioHelper::QuiesceRawReceive() {
    if (!bRawMode) return;
    if (receiveMutex != nullptr) xSemaphoreTake(receiveMutex, portMAX_DELAY);
    decoder.finishRawCapture();  // 先提交尾段，再停止射频，避免记录切换产生的边沿
    decoder.end();
    cc1101.setIdleMode();
    if (receiveMutex != nullptr) xSemaphoreGive(receiveMutex);
}

void RadioHelper::DisableRawReceive() {
    if (!bRawMode) return;
    LOG_I("DisableRawReceive");

    if (receiveMutex != nullptr) xSemaphoreTake(receiveMutex, portMAX_DELAY);
    bRawMode = false;
    decoder.enableRawMode(false);
    decoder.end();
    cc1101.setIdleMode();
    // 恢复常规接收寄存器基线（含灵敏度档 AGC/带宽）
    cc1101.restoreDefaultConfig();
    applyRxSensitivity(systemSetting.getRxSensitivity());
    if (receiveMutex != nullptr) xSemaphoreGive(receiveMutex);
}

int16_t RadioHelper::GetRawRssi() {
    if (!cc1101Ready) return -127;
    return cc1101.getRSSIdBm();
}

// 发送 RAW .sub 文件（共用：临时文件/边车文件同一加载+重放链路）
bool RadioHelper::sendRawSlotFile(int slot) {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return false;
    if (slot == 0) {
        return RawCaptureSession::ensureMounted() && sendRawSubFile(LittleFS, RAW_TEMP_FILE);
    }

    auto entry = dataStore.ReadSignal(SIG_RF, slot);
    if (!entry.rf.rawFlag) return false;
    char path[RawSignalFiles::PathSize];
    auto* fs = RawSignalFiles::locate(slot, entry.rf.rawFileId, path);
    return fs && sendRawSubFile(*fs, path);
}

bool RadioHelper::sendRawSubFile(fs::FS& fs, const char* path) {
    RawSubGhzReader reader;
    if (!cc1101Ready || !reader.open(fs, path) ||
        reader.frequencyHz < 300000000 || reader.frequencyHz > 460000000) return false;
    uint8_t preset = RfPresetIdFromName(reader.preset);
    if (strcmp(reader.preset, RfPreset(preset).flipperName) != 0) return false;
    if (bReceiveMode) DisableReceive();
    if (bRawMode) DisableRawReceive();
    setAntennaBand(bandOfFrequency(reader.frequencyHz / 1000000.0f));
    if (!cc1101.setFrequency(reader.frequencyHz)) return false;
    LOG_I("[TX.raw] file stream %luHz preset=%s", (unsigned long)reader.frequencyHz, reader.preset);
#if RF_RAW_TX_USE_RMT
    LOG_I("[TX.raw] backend=RMT");
    bool ok = cc1101.transmitRawStream(RawSubGhzReader::read, &reader, txPowerLevel, preset);
#else
    LOG_I("[TX.raw] backend=GPIO/GPTimer");
    bool ok = cc1101.transmitRawGpioStream(RawSubGhzReader::read, &reader, txPowerLevel, preset);
#endif
    cc1101.restoreDefaultConfig();
    return ok;
}

bool RadioHelper::SendRawTempFile() {
    RadioTransmitGuard guard(transmitBusy);
    if (!guard) return false;
    cc1101.resetTransmitCancel();
    LOG_I("[TX.raw] temp file: %s", RAW_TEMP_FILE);
    bool ok = sendRawSlotFile(0);
    if (ok) SoundAlertBeep(100);
    return ok;
}

bool RadioHelper::SendRawSlot(int slot1Based) {
    RadioTransmitGuard guard(transmitBusy);
    if (!guard) return false;
    cc1101.resetTransmitCancel();
    if (slot1Based < 1 || slot1Based > RF_SLOT_COUNT) return false;
    return sendRawSlotFile(slot1Based);
}

bool RadioHelper::StartRawTransmit(int slot) {
    if (!cc1101Ready || slot < 0 || slot > RF_SLOT_COUNT) return false;
    bool expected = false;
    if (!transmitBusy.compare_exchange_strong(expected, true)) return false;
    rawTransmitState.store(RawTransmitState::Sending);
    DisableReceive();
    DisableRawReceive();
    cc1101.resetTransmitCancel();
    rawTransmitSlot = slot;
    if (xTaskCreatePinnedToCore(rawTransmitTask, TASK_RAW_TX_NAME, TASK_RAW_TX_STACK,
                               this, TASK_PRIO_HW, nullptr, TASK_CORE_APP) != pdPASS) {
        rawTransmitState.store(RawTransmitState::Failed);
        transmitBusy.store(false);
        LOG_E("RAW transmit task creation failed");
        return false;
    }
    return true;
}

void RadioHelper::rawTransmitTask(void* context) {
    RadioHelper* self = static_cast<RadioHelper*>(context);
    bool ok = self->sendRawSlotFile(self->rawTransmitSlot);
    bool cancelled = self->cc1101.transmitCancelled();
    if (ok && !cancelled) SoundAlertBeep(100);
    LOG_I("[TX.raw] completed ok=%d cancelled=%d", ok, cancelled);
    // 所有硬件/文件访问结束后发布结果，最后释放所有权；随后不再访问self。
    self->rawTransmitState.store(cancelled ? RawTransmitState::Cancelled :
                                 ok ? RawTransmitState::Sent : RawTransmitState::Failed);
    self->transmitBusy.store(false);
    vTaskDelete(nullptr);
}

// 获取当前扫描频点（页面状态显示用）
float RadioHelper::GetCurrentScanFrequency()
{
    if (!bReceiveMode) return 0.0f;
    if (fixedFreqMode) return fixedFreqMHz;
    int idx = scanner.getCurrentFreqIndex();
    const float* table = (currentBand == RF_BAND_LOW) ? RF_FREQ_TABLE_LOW : RF_FREQ_TABLE_HIGH;
    int count = (currentBand == RF_BAND_LOW) ? RF_FREQ_COUNT_LOW : RF_FREQ_COUNT_HIGH;
    if (idx < 0 || idx >= count) return 0.0f;
    return table[idx];
}

// ──────────────────────────────────────────────────────────────────────────────
// 发射
// ──────────────────────────────────────────────────────────────────────────────
void RadioHelper::SetRepeatTransmit(int nRepeatTransmit)
{
    repeatCount = nRepeatTransmit > 0 && nRepeatTransmit <= 255 ? nRepeatTransmit : 15;
}

void RadioHelper::SetTxPowerLevel(int level)
{
    if (level < 0) level = 0;
    if (level > 7) level = 7;
    // Each TX path programs PATABLE before entering TX, so updates can be queued during a send.
    txPowerLevel = level;
    LOG_I("RadioHelper: 发射功率档 %d (%d dBm)", level, CC1101Driver::txPowerToDbm(level));
}

const char* RadioHelper::GetTxPowerText() const
{
    static char buf[24];
    const int power = txPowerLevel.load();
    snprintf(buf, sizeof(buf), "%d (%d dBm)", power, CC1101Driver::txPowerToDbm(power));
    return buf;
}

void RadioHelper::SetRxSensitivity(int level)
{
    if (IsTransmitting()) return;
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    if (cc1101Ready) applyRxSensitivity(level);
}

void RadioHelper::applyRxSensitivity(int level)
{
    // RX 灵敏度 = AGC 目标增益 + 接收带宽，基线对齐 Flipper OOK 异步预设。
    // 0=标准（Flipper 原文，最干净），1=高灵敏（增益更高），2=最高（最宽带宽+高增益，最吵）。
    if (level == 2) {
        cc1101.writeReg(CC1101_AGCCTRL2, 0x07);  // 更高目标增益
        cc1101.writeReg(CC1101_AGCCTRL1, 0x00);
        cc1101.writeReg(CC1101_AGCCTRL0, 0x91);  // Flipper OOK650：8dB 判决边界/中等滞回
        cc1101.writeReg(CC1101_MDMCFG4, 0x17);   // BW 650kHz（Flipper ook_650k 预设）
    } else if (level == 1) {
        cc1101.writeReg(CC1101_AGCCTRL2, 0x07);  // 更高目标增益
        cc1101.writeReg(CC1101_AGCCTRL1, 0x00);
        cc1101.writeReg(CC1101_AGCCTRL0, 0x40);  // 保持快响应
        cc1101.writeReg(CC1101_MDMCFG4, 0x67);   // BW 270kHz
    } else {
        cc1101.writeReg(CC1101_AGCCTRL2, 0x03);  // Flipper OOK 预设原文
        cc1101.writeReg(CC1101_AGCCTRL1, 0x00);
        cc1101.writeReg(CC1101_AGCCTRL0, 0x40);
        cc1101.writeReg(CC1101_MDMCFG4, 0x67);   // BW 270kHz
    }
    cc1101.writeReg(CC1101_FIFOTHR, level == 2 ? 0x07 : 0x47);
    cc1101.writeReg(CC1101_TEST2, level == 2 ? 0x88 : 0x81);
    cc1101.writeReg(CC1101_TEST1, level == 2 ? 0x31 : 0x35);
    cc1101.writeReg(CC1101_TEST0, 0x09);
    LOG_I("RadioHelper: 接收灵敏度档 %d", level);
}

bool RadioHelper::SendData(const RFSignalData& data)
{
    LOG_I("SendData (CC1101)");

    if (!cc1101Ready) {
        LOG_E("SendData: CC1101未就绪!");
        return false;
    }

    // RAW 录制信号：脉冲在边车文件，按槽位读回原样重放
    if (data.rawFlag) {
        if (data.rawSlot < 1) {
            LOG_E("SendData: RAW 信号缺少槽位号");
            return false;
        }
        return StartRawTransmit(data.rawSlot);
    }

    RadioTransmitGuard guard(transmitBusy);
    if (!guard) return false;
    const int repeats = repeatCount;
    const int power = txPowerLevel;
    cc1101.resetTransmitCancel();
    if (!data.isValid() || data.bitCount > sizeof(data.data) * 8 ||
        !(data.frequency >= 300.0f && data.frequency <= 1000.0f)) {
        LOG_E("SendData: 无效信号数据!");
        return false;
    }

    // 先禁用接收模式，避免与发送冲突
    if (bReceiveMode) {
        DisableReceive();
    }
    if (bRawMode) DisableRawReceive();

    // 天线开关切换到信号所属频段
    setAntennaBand(bandOfFrequency(data.frequency));

    // 设置发射频率
    if (!cc1101.setFrequencyMHz(data.frequency)) {
        LOG_E("[TX] Cannot tune %.2fMHz", data.frequency);
        return false;
    }

    LOG_I("[TX] %.2fMHz %dbits S=%d L=%d SYNC=%d x%d PWR=%d(%ddBm) ANT=%s",
          data.frequency, data.bitCount,
          data.shortPulse, data.longPulse, data.syncPulse, repeats,
          power, CC1101Driver::txPowerToDbm(power),
          bandOfFrequency(data.frequency) == RF_BAND_LOW ? "LOW" : "HIGH");

    // ── 协议分流 ──
    // CameStyle 协议（CAME / NICE FLO / HOLTEK）：数据位结构与 PT2262 相反
    //   （有 start bit、(low,high) 位对、teLong=2Te、帧头 Guard 做帧间隔），
    //   必须用 Flipper encoder 生成段序列后走通用脉冲发射 transmitPulses；
    //   其余协议保持原有 transmitPT2262 路径（bit = (high,low) 对 + 帧尾 sync）。
    const CameStyleProtoConst* cameProto = findCameStyleConst(data.protocolName);
    bool txOk = false;
    if (cameProto != nullptr && data.bitCount <= 64) {
        // data[32] MSB first → uint64 key（CameStyle 协议位数 ≤64）
        uint64_t key = 0;
        for (uint16_t i = 0; i < data.bitCount; i++) {
            if (data.data[i / 8] & (uint8_t)(1 << (7 - (i % 8))))
                key |= (1ULL << (data.bitCount - 1 - i));
        }
        static uint16_t sDurations[2 * 64 + 2];
        static bool     sLevels[2 * 64 + 2];
        uint16_t n = CameStyleProtocolDecoder::buildFrame(
            cameProto, key, (uint8_t)data.bitCount, data.shortPulse,
            sDurations, sLevels);
        if (n > 0) {
            txOk = cc1101.transmitPulses(sDurations, sLevels, n,
                                         (uint8_t)repeats, power);
        }
        if (!txOk) {
            LOG_E("[TX] ERROR: transmitPulses failed (%s)!", cameProto->name);
        }
    } else {
        // RMT精确时序发射（内部会保存/恢复频率寄存器，支持任意频点）
        txOk = cc1101.transmitPT2262(data.data, data.bitCount,
                                     data.shortPulse, data.longPulse,
                                     data.syncPulse, (uint8_t)repeats, power);
        if (!txOk) {
            LOG_E("[TX] ERROR: transmitPT2262 failed!");
        }
    }

    // 恢复默认接收配置（保留当前频率）
    cc1101.restoreDefaultConfig();

    // 非阻塞提示音（WM8960滴声）
    if (txOk) SoundAlertBeep(100);
    LOG_I("[TX] result=%s", txOk ? "sent" : "failed");
    return txOk;
}

// ─────────────────────────────────────────────────────────────────────────────
// 直接发射 Flipper .sub 记录（RAW / 协议型统一入口）
// RAW 型：rawData 正=high(载波开)/负=low → transmitPulses 原样重放；
// 协议型：Princeton/SMC5326 转 PT2262 时序参数走 transmitPT2262，
//         CAME/Nice FLO/Holtek 走 buildFrame + transmitPulses。
// ─────────────────────────────────────────────────────────────────────────────
bool RadioHelper::SendSubGhzRecord(const SubGhzKeyRecord& rec) {
    RadioTransmitGuard guard(transmitBusy);
    if (!guard) return false;
    cc1101.resetTransmitCancel();
    return sendSubGhzRecord(rec);
}

bool RadioHelper::sendSubGhzRecord(const SubGhzKeyRecord& rec) {
    const int repeats = repeatCount;
    const int power = txPowerLevel;
    if (!cc1101Ready) {
        LOG_E("SendSubGhzRecord: CC1101未就绪!");
        return false;
    }
    float freqMHz = rec.frequencyHz / 1000000.0f;
    if (freqMHz < 300.0f || freqMHz > 460.0f) {
        LOG_E("SendSubGhzRecord: 频率越界 %.2fMHz", freqMHz);
        return false;
    }

    if (bReceiveMode) DisableReceive();
    if (bRawMode) DisableRawReceive();
    setAntennaBand(bandOfFrequency(freqMHz));
    if (!cc1101.setFrequency(rec.frequencyHz)) return false;

    // Preset → 发射调制路径（仅 RAW 段序列需要；协议型全为 OOK）
    uint8_t txMod = TXMOD_OOK;
    if (rec.isRaw) {
        if (strstr(rec.preset, "2FSKDev476") != nullptr) {
            txMod = TXMOD_2FSK_47K6;
        } else if (strstr(rec.preset, "2FSK") != nullptr) {
            txMod = TXMOD_2FSK_2K38;
        }
    }

    LOG_I("[TX.sub] %.2fMHz %s x%d PWR=%d mod=%u",
          freqMHz, rec.protocol, rec.isRaw ? 1 : repeats, power, (unsigned)txMod);

    bool ok = false;

    if (rec.isRaw) {
        if (!rec.rawData || rec.rawCount == 0 || rec.rawCount > SUB_RAW_MAX_PULSES) {
            cc1101.restoreDefaultConfig();
            return false;
        }
        // RAW文件已包含帧间隔和重复帧，直接使用有符号32位时长及原预设。
        const uint8_t presetId = RfPresetIdFromName(rec.preset);
        if (strcmp(RfPreset(presetId).flipperName, rec.preset) != 0) {
            LOG_E("[TX.raw] unsupported preset: %s", rec.preset);
        } else {
            uint64_t durationUs = 0;
            for (uint16_t i = 0; i < rec.rawCount; ++i) {
                int64_t v = rec.rawData[i];
                durationUs += v < 0 ? -v : v;
            }
            LOG_I("[TX.raw] pulses=%u duration=%lluus preset=%s", rec.rawCount,
                  (unsigned long long)durationUs, rec.preset);
            ok = cc1101.transmitRaw(rec.rawData, rec.rawCount, power, presetId);
        }
        if (!ok) LOG_E("[TX.sub] RAW transmitPulses failed!");
    } else {
        // 协议型：先转设备条目，再按 SendData 同款分流逻辑发射
        SignalEntry entry;
        if (subGhzRecordToSignalEntry(rec, entry)) {
            const CameStyleProtoConst* cameProto = findCameStyleConst(entry.rf.protocolName);
            if (cameProto != nullptr) {
                uint64_t key = 0;
                for (uint16_t i = 0; i < entry.rf.bitCount; i++) {
                    if (entry.rf.data[i / 8] & (uint8_t)(1 << (7 - (i % 8))))
                        key |= (1ULL << (entry.rf.bitCount - 1 - i));
                }
                static uint16_t sDurations[2 * 64 + 2];
                static bool     sLevels[2 * 64 + 2];
                uint16_t n = CameStyleProtocolDecoder::buildFrame(
                    cameProto, key, (uint8_t)entry.rf.bitCount, entry.rf.shortPulse,
                    sDurations, sLevels);
                if (n > 0) {
                    ok = cc1101.transmitPulses(sDurations, sLevels, n,
                                               (uint8_t)repeats, power);
                }
                if (!ok) LOG_E("[TX.sub] %s transmitPulses failed!", cameProto->name);
            } else {
                // Princeton / SMC5326：短=Te、长=3Te、帧尾 Guard=32Te
                ok = cc1101.transmitPT2262(entry.rf.data, entry.rf.bitCount,
                                           entry.rf.shortPulse, entry.rf.longPulse,
                                           entry.rf.syncPulse,
                                           (uint8_t)repeats, power);
                if (!ok) LOG_E("[TX.sub] %s transmitPT2262 failed!", entry.rf.protocolName);
            }
        } else {
            LOG_E("[TX.sub] 协议不支持发射: %s", rec.protocol);
        }
    }

    cc1101.restoreDefaultConfig();
    return ok;
}

// ──────────────────────────────────────────────────────────────────────────────
// 接收任务：循环驱动扫描器状态机，确认信号后写入rcData
// ──────────────────────────────────────────────────────────────────────────────
void RadioHelper::radioReceiveTask(void* pvParameters)
{
    RadioHelper* helper = static_cast<RadioHelper*>(pvParameters);

    while (true) {
        // 等待信号量，表示需要开始接收
        if (xSemaphoreTake(helper->receiveStartSemaphore, portMAX_DELAY) == pdTRUE) {
            uint32_t diagnosticAt = millis();
            uint32_t lastEdges = helper->decoder.edgeCount();
            int16_t peakRssi = -127;
            while (helper->bReceiveMode) {
                bool gotSignal = false;

                if (helper->receiveMutex != nullptr) {
                    if (xSemaphoreTake(helper->receiveMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        if (!helper->bReceiveMode) {
                            xSemaphoreGive(helper->receiveMutex);
                            break;
                        }

                        // 驱动扫描器状态机（快速扫描/信号确认）
                        helper->scanner.loop();
                        // 已确认帧优先交付，不插入每秒诊断或预设切换日志。
                        if (!helper->scanner.hasConfirmedSignal()) {
                            if (!helper->scanner.isRunning()) {
                                helper->bReceiveMode = false;
                                LOG_E("RX stopped: tuning / RX state failed");
                            }
                            int16_t rssi = helper->cc1101.getRSSIdBm();
                            if (rssi > peakRssi) peakRssi = rssi;
                            if (millis() - diagnosticAt >= 1000) {
                                uint32_t edges = helper->decoder.edgeCount();
                                LOG_I("[RX.live] freq=%.2fMHz MARC=%02X RSSI=%d peak=%d edges=%lu/s GDO0/2=%d/%d overruns=%lu elapsed=%lums",
                                      helper->GetCurrentScanFrequency(), helper->cc1101.getMARCState(),
                                      rssi, peakRssi, (unsigned long)(edges - lastEdges),
                                      digitalRead(PIN_CC1101_GDO0), digitalRead(PIN_CC1101_GDO2),
                                      (unsigned long)helper->decoder.receiveOverruns(),
                                      (unsigned long)(helper->fixedFreqMode ? millis() - helper->fixedRxSessionAt : 0));
                                helper->decoder.logReceiveDiagnostics(peakRssi >= -45);
                                diagnosticAt = millis();
                                lastEdges = edges;
                                peakRssi = -127;
                            }
                        }

                        // 检查是否有已确认的信号帧
                        SignalFrame frame;
                        if (helper->scanner.getConfirmedSignal(frame)) {
                            RFSignalData& out = helper->rcData;
                            memset(&out, 0, sizeof(out));

                            // 频率：单频守候直接取守候频点，否则由频点索引换算
                            if (helper->fixedFreqMode) {
                                out.frequency = helper->fixedFreqMHz;
                            } else {
                                const float* table = (helper->currentBand == RF_BAND_LOW)
                                                     ? RF_FREQ_TABLE_LOW : RF_FREQ_TABLE_HIGH;
                                out.frequency = table[frame.freqIndex];
                            }
                            out.bitCount    = frame.bitCount;
                            out.shortPulse  = frame.shortPulse;
                            out.longPulse   = frame.longPulse;
                            out.syncPulse   = frame.syncPulse;
                            memcpy(out.data, frame.data, sizeof(out.data));
                            // 协议名必须随已确认帧走，不能取后来一包的推测结果。
                            strncpy(out.protocolName, frame.protocolName,
                                    sizeof(out.protocolName) - 1);

                            // 先交付UI，后输出日志；Serial回压不能延迟结果可见。
                            helper->scanner.stop();
                            helper->hasNewSignal = true;
                            helper->bReceiveMode = false;
                            gotSignal = true;

                            char hex[MAX_BITS / 4 + 1];
                            RFSignalDecoder::pulsesToHex(out.data, out.bitCount, hex);
                            LOG_I("[RX] %.2fMHz %dbits data=%s S=%d L=%d SYNC=%d Guard=%lu proto=%s elapsed=%lums",
                                  out.frequency, out.bitCount, hex,
                                  out.shortPulse, out.longPulse, out.syncPulse,
                                  (unsigned long)frame.guardDurationUs,
                                  out.protocolName,
                                  (unsigned long)(helper->fixedFreqMode ? millis() - helper->fixedRxSessionAt : 0));
                            LOG_D("[RX] task stack free=%u words",
                                  (unsigned)uxTaskGetStackHighWaterMark(NULL));

                        }

                        xSemaphoreGive(helper->receiveMutex);
                    }
                }

                if (gotSignal) {
                    // 接收成功提示音（互斥锁外调用）
                    SoundAlertBeep(500);
                    break;  // 等待下一次EnableReceive
                }

                // 扫描器内部按dwell时间切换频点，此处短延时避免过度占用CPU
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        }
    }
}
