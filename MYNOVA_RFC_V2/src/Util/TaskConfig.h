/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// TaskConfig.h —— 任务台账: 名/核/栈/优先级的单一事实来源（规划 §四 双核分工）
// 分工原则:
//   Core0(TASK_CORE_SYSTEM) = 系统/无线/音频核: WiFi/lwIP/Web/OTA/电池/音频I2S
//   Core1(TASK_CORE_APP)    = 交互/业务核: UI/按键/RF接收/IR学习/对讲协议
// 优先级总表(应用层 1..4, 不许高于 UI):
//   4=UIKernel交互  3=硬时序业务  2=网络worker  1=后台
// 约定: 应用任务一律 xTaskCreatePinnedToCore, 禁止无亲和 xTaskCreate。
#ifndef UTIL_TASKCONFIG_H
#define UTIL_TASKCONFIG_H

// ── 核 ──────────────────────────────────────────────
#define TASK_CORE_SYSTEM   0
#define TASK_CORE_APP      1

// ── 优先级 ───────────────────────────────────────────
#define TASK_PRIO_UI       4   // UIKernel(唯一操作UI的任务)
#define TASK_PRIO_HW       3   // RF/IR/Talk/Audio 等硬时序业务
#define TASK_PRIO_NET      2   // WiFiWorker
#define TASK_PRIO_BG       1   // 电池/OTA/loopTask等后台

// ── 任务参数台账 ─────────────────────────────────────
// UIKernel(Core1): 输入分发→导航→update→render→sendBuffer 串行
#define TASK_UI_NAME       "UIKernel"
#define TASK_UI_STACK      8192

// RF接收(Core1): 与GDO2 ISR同核
// 栈12KB为实测校准值(脉冲拷贝+解码直方图+printf浮点, 6KB触发栈看门狗)
#define TASK_RADIO_NAME    "RadioRx"
#define TASK_RADIO_STACK   12288

// RAW落盘(Core1): 独立于OLED刷新，2ms排空GDO2脉冲环形缓冲
#define TASK_RAW_NAME      "RawCapture"
#define TASK_RAW_STACK     4096

// RAW回放(Core1): 文件预读/取消在worker中，GPIO定时器或RMT负责输出
#define TASK_RAW_TX_NAME    "RawTx"
#define TASK_RAW_TX_STACK   4096

// IR学习(Core1): 3ms轮询decode(), 与IR捕获ISR同核
#define TASK_IR_NAME       "IRLearn"
#define TASK_IR_STACK      4096

// 对讲协议状态机(Core1): 配对/重传/心跳/文字
#define TASK_TALK_NAME     "TalkTask"
#define TASK_TALK_STACK    8192

// 音频引擎(Core0): 解码/I2S供给/录音(维持原钉核)
#define TASK_AUDIO_NAME    "AudioTask"
#define TASK_AUDIO_STACK   16384

// WiFi worker(Core0): scan/connect/AP 三合一持久任务
#define TASK_WIFI_NAME     "WiFiWorker"
#define TASK_WIFI_STACK    6144

// OTA 下载(Core0): 网络属性归系统核
#define TASK_OTA_NAME      "OtaTask"
#define TASK_OTA_STACK     8192

// 电池监测(Core0): 慢周期ADC采样
#define TASK_BATT_NAME     "BattMon"
#define TASK_BATT_STACK    4096

#endif // UTIL_TASKCONFIG_H
