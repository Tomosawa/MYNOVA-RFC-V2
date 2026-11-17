/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef __RADIOHELPER_H__
#define __RADIOHELPER_H__

#include <Arduino.h>
#include <FS.h>
#include "../Proto/SignalData.h"
#include "../Lib/CC1101Driver.h"
#include "../Lib/RFSignalDecoder.h"
#include "../Lib/FrequencyScanner.h"
#include "../Lib/FlipperSignalFile.h"

// ==================== 频段定义 ====================
// CC1101覆盖的扫描频段，由外部天线开关(RF_SW_CTRL)切换高低频通路
enum RFBand {
    RF_BAND_LOW = 0,    // 低频段 310~350MHz
    RF_BAND_HIGH = 1    // 高频段 390~434MHz
};

// ==================== 扫描频率表（MHz） ====================
// 覆盖全球主流 OOK 遥控频段：
//   低频段 310-350 : 美国/日本部分车库门、旧式门铃
//   高频段 390-434 : 北美/欧洲/中国车库门与遥控器（最常用）
extern const float RF_FREQ_TABLE_LOW[];
extern const int   RF_FREQ_COUNT_LOW;
extern const float RF_FREQ_TABLE_HIGH[];
extern const int   RF_FREQ_COUNT_HIGH;
// 全部频点（低频在前高频在后，编辑页面频率选择用）
extern const float RF_FREQ_TABLE_ALL[];
extern const int   RF_FREQ_COUNT_ALL;

// 根据频率值判断所属频段（≤360MHz视为低频段）
RFBand RFGetBandByFrequency(float freqMHz);

enum class RawTransmitState : uint8_t { Idle, Sending, Sent, Failed, Cancelled };

class RadioHelper
{
public:
    RadioHelper();
    ~RadioHelper();
    void init();

    // ==================== 接收 ====================
    void EnableReceive(RFBand band);    // 启动指定频段的扫描接收
    void EnableReceiveFixed(float freqMHz); // 单频点守候接收（天线开关按频率自动切换）
    void DisableReceive();              // 停止接收
    bool IsReceiving() const { return bReceiveMode; }
    float GetCurrentScanFrequency();    // 当前扫描频点（页面显示用）

    // ==================== RAW 连续录制接收 ====================
    // 直接配置频点+调制预设并进入异步 RX，解码器切 RAW 环形模式（与扫描接收互斥）
    bool EnableRawReceive(float freqMHz, uint8_t presetId);
    void QuiesceRawReceive();          // 停止产生新脉冲，保留环形缓冲供录制任务排空
    void DisableRawReceive();
    bool IsRawReceiving() const { return bRawMode; }
    int16_t GetRawRssi();               // 录制页 RSSI 采样用
    RFSignalDecoder& GetDecoder() { return decoder; }

    // 发送录制临时文件（保存前回放，/raw_tmp.sub）
    bool SendRawTempFile();
    // 发送已保存槽位的 RAW 边车文件 /rfraw_{slot}.sub
    bool SendRawSlot(int slot1Based);

    // slot=0回放临时录制；1..100回放槽位。任务仅持有RadioHelper，不持有页面指针。
    bool StartRawTransmit(int slot = 0);
    void CancelRawTransmit() { cc1101.cancelTransmit(); }
    RawTransmitState GetRawTransmitState() const { return rawTransmitState.load(); }
    bool IsTransmitting() const { return transmitBusy.load(); }
    bool IsRawTransmitting() const { return GetRawTransmitState() == RawTransmitState::Sending; }

    // ==================== 发射 ====================
    void SetRepeatTransmit(int nRepeatTransmit);
    void SetTxPowerLevel(int level);   // 0-7，7=最大
    void SetRxSensitivity(int level);  // 0-2，2=最灵敏
    int GetTxPowerLevel() const { return txPowerLevel; }
    const char* GetTxPowerText() const;
    bool SendData(const RFSignalData& data);

    // 直接发射 Flipper .sub 记录（RAW 型走 transmitPulses；协议型按协议分流：
    // CAME/Nice FLO/Holtek 用 Flipper encoder，Princeton/SMC5326 转 PT2262 时序）
    // 使用当前 repeatCount/txPowerLevel 设置
    bool SendSubGhzRecord(const SubGhzKeyRecord& rec);

public:
    // 接收结果（接收任务在receiveMutex锁内写入，置hasNewSignal后停止接收不再改写）
    // ── 跨核数据规约(规划§4.6) ──
    // 生命周期约定: rcData仅在 hasNewSignal==true 置位后有效;
    // 读方(UIKernel页面上下文)先读数据再ClearNewSignal; 下一次EnableReceive
    // 会先清空rcData——而EnableReceive同样由UIKernel调用, 读方与写方
    // 天然互斥, 故读取无需加锁(接收任务写时持receiveMutex, 且置位后即停写)。
    RFSignalData rcData;
    std::atomic<bool> hasNewSignal;         // 新信号标志（页面读取后需调用ClearNewSignal）

    void ClearNewSignal() { hasNewSignal = false; }

private:
    std::atomic<bool> transmitBusy{false};
    std::atomic<RawTransmitState> rawTransmitState{RawTransmitState::Idle};
    int rawTransmitSlot = 0;
    static void rawTransmitTask(void* context);
    bool sendRawSlotFile(int slot);
    bool sendRawSubFile(fs::FS& fs, const char* path);
    bool sendSubGhzRecord(const SubGhzKeyRecord& rec);
    bool initCC1101();                  // CC1101初始化（含AGC灵敏度优化）
    bool configureReceive(float freqMHz, uint8_t presetId, bool sensitivity);
    bool beginOokReceive(const float* frequencies, int count);
    void logReceiveState(const char* mode);
    void applyRxSensitivity(int level); // 按档位写 AGC/带宽寄存器
    void setAntennaBand(RFBand band);   // 设置外部天线开关
    RFBand bandOfFrequency(float freqMHz);

    CC1101Driver     cc1101;
    RFSignalDecoder  decoder;
    FrequencyScanner scanner;

    std::atomic<bool> bReceiveMode;
    bool bRawMode;                    // RAW 连续录制模式（与扫描接收互斥）
    bool cc1101Ready;
    RFBand currentBand;
    float fixedFreqMHz;   // 单频点守候时的频点（scanner.begin 持有指针用）
    bool fixedFreqMode;   // true=单频点守候，频率显示/上报走 fixedFreqMHz
    uint32_t fixedRxSessionAt = 0;
    std::atomic<int> repeatCount;                    // 发射重复次数
    std::atomic<int> txPowerLevel;                   // 发射功率档 0-7（默认7=最大）

    // FreeRTOS相关成员
    SemaphoreHandle_t receiveMutex;         // 保护接收状态
    SemaphoreHandle_t receiveStartSemaphore;
    TaskHandle_t radioReceiveTaskHandle;

    // 接收任务函数
    static void radioReceiveTask(void* pvParameters);
};

#endif
