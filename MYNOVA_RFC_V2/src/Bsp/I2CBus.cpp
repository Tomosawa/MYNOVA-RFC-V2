/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

#include "I2CBus.h"

SemaphoreHandle_t I2CBus::s_mutex = nullptr;

SemaphoreHandle_t I2CBus::begin() {
    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutex();
    }
    return s_mutex;
}

bool I2CBus::lock(TickType_t timeout) {
    if (s_mutex == nullptr) return true;  // 未初始化按直通(单线程期)
    return xSemaphoreTake(s_mutex, timeout) == pdTRUE;
}

void I2CBus::unlock() {
    if (s_mutex != nullptr) {
        xSemaphoreGive(s_mutex);
    }
}
