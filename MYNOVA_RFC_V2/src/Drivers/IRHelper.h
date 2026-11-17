/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef __IRHELPER_H__
#define __IRHELPER_H__

#include <Arduino.h>
#include <mutex>
#include "../Proto/SignalData.h"
#include <IRrecv.h>
#include <IRsend.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>
#include <IRac.h>          // IRAcUtils::resultAcToString: 空调协议可读状态描述
#include <esp32-hal-rmt.h> // Arduino 3.x RMT，避免与射频IDF新驱动混入legacy类型

// 红外学习完成后的等待判定时间：
// 收到最后一帧后，静默超过该时间仍无新帧则认为整组信号（可能多帧）发送完毕
#define IR_LEARN_SETTLE_MS  300

// ─── IR发射方式（P0清理决策: 固化为RMT硬件发射） ─────────────────────────────
// 1 = RMT硬件发射: 1us时序精度 + RMT硬件38kHz载波（零CPU占用）——正式路径
// RMT初始化失败时自动降级IRremoteESP8266软件sendRaw（sendFrameRMT不可用
// 时由SendData内部fallback，精度受中断抖动影响但保证可用）。
// S3有4个TX通道，与CC1101的RMT发射(GDO0)各占一个，无冲突。
#define IR_TX_USE_RMT  1

// IRrecv捕获缓冲大小（单位: 2us tick）
// 1024 tick ≈ 511 bit，足以捕获最大AC协议单帧（Mitsubishi 144bit等）
#define IR_CAPTURE_BUF_SIZE  1024

// IRrecv单帧内静默超时(ms)：
// 15ms为库标准值——让不同遥控码（NEC标准帧间隔约42ms）分离为独立帧。
// 注：帧内间隔>15ms的空调协议会被拆成多段，但多帧重放架构会按原始
//     间隔（精确到ms）逐段拼接重发，重放波形与原始信号等效。
#define IR_CAPTURE_TIMEOUT_MS  15

class IRHelper
{
public:
    IRHelper();
    ~IRHelper();
    void init();

    // ==================== 学习（接收） ====================
    void EnableReceive();       // 开始红外学习（学习任务自动启动捕获）
    void DisableReceive();      // 停止学习
    bool IsReceiving() const { return learnCmd == 1; }

    // 轮询：静默超时后判定整组完成（帧处理由内部学习任务实时进行）
    // 返回true时学习结果已就绪（保存在内部，供保存流程读取）
    bool CheckLearnFinished();

    // 学习结果（元数据+边沿数据，在下次EnableReceive前持续有效）
    const IRSignalMeta& getMeta() const { return meta; }
    const uint16_t* getEdges() const { return edges; }

    // 当前已捕获帧数（页面实时显示"已收N帧"用）
    uint8_t GetReceivedFrameCount() const { return meta.frameCount; }

    // 最近一次新帧的时间戳（页面做RX活动闪烁指示用）
    unsigned long GetLastFrameTime() const { return lastFrameTime; }

    // ==================== 发射 ====================
    // 按槽位发射（从DataStore读取边沿数据后逐帧重放）
    bool SendFromSlot(int index);
    // 直接发射（边沿数据由调用方提供）
    bool SendData(const IRSignalMeta& meta, const uint16_t* edges);

    bool isReady() const { return bReady; }

private:
    std::mutex transmitMutex;
    // 处理一帧新捕获的消息（提取raw时序拼接到edges），返回是否已存入
    bool processCapturedFrame();
    // RMT硬件发射一帧（1us精度时序 + 38kHz硬件载波调制，零CPU占用）
    bool sendFrameRMT(const uint16_t* frameEdges, uint16_t len);
    // 红外学习任务：快速轮询decode()并驱动捕获启停
    // （脱离UI的100ms渲染节拍，保证快速连续帧不丢失）
    static void irLearnTaskFunc(void* pvParameters);

    IRrecv       irrecv;    // 红外接收器（IRremoteESP8266，中断捕获+协议解码）
    IRsend       irsend;    // 红外软件发射器（仅RMT初始化失败时的降级备胎）
    decode_results results; // 最近一次解码结果（仅学习任务使用）

    volatile uint8_t learnCmd;          // 学习命令：0=停止 1=启动（任务沿边沿动作）
    volatile bool bCaptureActive;       // 捕获是否实际运行中（任务维护）
    TaskHandle_t learnTaskHandle;       // 学习任务句柄
    bool bReady;
    bool rmtReady;                      // RMT发射通道是否就绪
    volatile unsigned long lastFrameTime;   // 最近一次新帧时间（静默判定基准）

    IRSignalMeta meta;              // 学习结果元数据
    uint16_t* edges;                // 学习结果边沿缓冲（8帧×512边沿=8KB，init时一次性分配）
};

#endif
