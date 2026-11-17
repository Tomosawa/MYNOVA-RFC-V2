/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// HeapMonitor.cpp —— 双堆与任务栈水位监控实现
#include "HeapMonitor.h"
#include "Log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define HM_TAG "HEAP"
#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG HM_TAG

uint32_t HeapMonitor::s_lastTickMs = 0;

HeapStat HeapMonitor::getStats() {
    HeapStat s;
    s.ramFree     = (uint32_t)ESP.getFreeHeap();
    s.ramMinFree  = (uint32_t)ESP.getMinFreeHeap();
    s.ramMaxAlloc = (uint32_t)ESP.getMaxAllocHeap();
    s.psFree      = (uint32_t)ESP.getFreePsram();
    s.psMinFree   = (uint32_t)ESP.getMinFreePsram();
    s.psMaxAlloc  = (uint32_t)ESP.getMaxAllocPsram();
    return s;
}

void HeapMonitor::printStats(const char* prefix) {
    HeapStat s = getStats();
    LOG_I("%s RAM free=%u min=%u maxAlloc=%u | PSRAM free=%u min=%u maxAlloc=%u",
          prefix,
          (unsigned)s.ramFree, (unsigned)s.ramMinFree, (unsigned)s.ramMaxAlloc,
          (unsigned)s.psFree, (unsigned)s.psMinFree, (unsigned)s.psMaxAlloc);
}

void HeapMonitor::begin() {
    printStats("boot:");
    // 上报一次任务表, 核对落核/优先级与台账一致
    reportTasks();
}

void HeapMonitor::tick() {
    uint32_t now = millis();
    if (s_lastTickMs != 0 && (uint32_t)(now - s_lastTickMs) < REPORT_PERIOD_MS) {
        return;
    }
    s_lastTickMs = now;
    printStats("30s :");
}

void HeapMonitor::reportTasks() {
    // uxTaskGetSystemState 需要内核开启 trace facility(ESP32 Arduino 默认开启)
    static TaskStatus_t taskStats[24];
    UBaseType_t count = uxTaskGetSystemState(taskStats, 24, nullptr);
    if (count == 0) {
        LOG_W("reportTasks: uxTaskGetSystemState 不可用");
        return;
    }
    LOG_I("---- task table (name/core/stackHWM/prio/state) ----");
    for (UBaseType_t i = 0; i < count; i++) {
        const TaskStatus_t& t = taskStats[i];
        LOG_I("%-16s C%d stk=%u prio=%u st=%d",
              t.pcTaskName,
              (int)t.xCoreID,
              (unsigned)t.usStackHighWaterMark,
              (unsigned)t.uxCurrentPriority,
              (int)t.eCurrentState);
    }
}
