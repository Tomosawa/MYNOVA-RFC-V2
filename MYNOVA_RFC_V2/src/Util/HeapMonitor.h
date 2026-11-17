/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// HeapMonitor.h —— 双堆(内部RAM+PSRAM)与任务栈水位监控（规划 §5.4 / §4.7）
// 启动横幅 + 周期上报 + 任务表核对(P3双核落位验证), 全部走 Log。
#ifndef UTIL_HEAPMONITOR_H
#define UTIL_HEAPMONITOR_H

#include <Arduino.h>

struct HeapStat {
    uint32_t ramFree;      // 内部RAM当前空闲
    uint32_t ramMinFree;   // 内部RAM历史最低
    uint32_t ramMaxAlloc;  // 内部RAM最大连续块
    uint32_t psFree;       // PSRAM当前空闲
    uint32_t psMinFree;    // PSRAM历史最低
    uint32_t psMaxAlloc;   // PSRAM最大连续块
};

class HeapMonitor {
public:
    // 启动时调用: 打印初始双堆横幅
    static void begin();
    // 主循环里周期调用(内部30秒节流, 无开销压力)
    static void tick();
    // 立即获取双堆快照(版本页诊断区等)
    static HeapStat getStats();
    // 打印全部任务表(名/运行核/栈高水线/优先级/状态) —— 双核落位核对用
    static void reportTasks();
private:
    static void printStats(const char* prefix);
    static uint32_t s_lastTickMs;
    static constexpr uint32_t REPORT_PERIOD_MS = 30000;
};

#endif // UTIL_HEAPMONITOR_H
