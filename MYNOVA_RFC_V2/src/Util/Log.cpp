/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// Log.cpp —— 分级日志实现
#include "Log.h"
#include <Preferences.h>
#include "../Services/DataStore.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern DataStore dataStore;

std::atomic<LogLevel> Log::s_level{LogLevel::INFO};
void* Log::s_mutex = nullptr;

const char* Log::levelName(LogLevel lv) {
    switch (lv) {
        case LogLevel::ERROR: return "E";
        case LogLevel::WARN:  return "W";
        case LogLevel::INFO:  return "I";
        case LogLevel::DEBUG: return "D";
        default:              return "-";
    }
}

void Log::reloadLevel() {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return;
    s_level = LogLevel::INFO;
    Preferences pf;
    if (pf.begin(NVS_NS, true)) {
        uint8_t lv = pf.getUChar(NVS_KEY, (uint8_t)LogLevel::INFO);
        pf.end();
        if (lv >= (uint8_t)LogLevel::NONE && lv <= (uint8_t)LogLevel::DEBUG) {
            s_level = (LogLevel)lv;
        }
    }
}

void Log::begin() {
    reloadLevel();
    if (s_mutex == nullptr) {
        s_mutex = (void*)xSemaphoreCreateMutex();
    }
    // 启动横幅: 让用户知道当前日志级别
    Serial.printf("[I][LOG] Log ready, level=%s (serial cmd: LOG:0-4)\n",
                  levelName(s_level));
}

void Log::setLevel(LogLevel lv, bool persist) {
    if (persist) {
        DataStore::StorageGuard storage(dataStore);
        if (!storage) return;
        Preferences pf;
        if (pf.begin(NVS_NS, false)) {
            pf.putUChar(NVS_KEY, (uint8_t)lv);
            pf.end();
        }
        s_level = lv;
    } else {
        s_level = lv;
    }
}

LogLevel Log::getLevel() {
    return s_level;
}

void Log::vOutput(const char* tag, LogLevel lv, const char* fmt, va_list ap) {
    // 运行期过滤
    if (lv > s_level.load()) {
        return;
    }
    // 多任务并发打印互斥(Serial 非线程安全)
    SemaphoreHandle_t mtx = (SemaphoreHandle_t)s_mutex;
    BaseType_t taken = pdFALSE;
    if (mtx != nullptr) {
        taken = xSemaphoreTake(mtx, pdMS_TO_TICKS(500));
    }
    Serial.printf("[%s][%s] ", levelName(lv), tag);
    Serial.vprintf(fmt, ap);
    Serial.print('\n');
    if (mtx != nullptr && taken == pdTRUE) {
        xSemaphoreGive(mtx);
    }
}

void Log::output(const char* tag, LogLevel lv, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vOutput(tag, lv, fmt, ap);
    va_end(ap);
}
