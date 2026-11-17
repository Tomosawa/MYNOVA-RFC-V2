/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

#include "PowerManager.h"

ISuspendable* PowerManager::s_devs[MAX_DEVS] = {nullptr};
int PowerManager::s_count = 0;

void PowerManager::registerSuspendable(ISuspendable* dev) {
    if (dev == nullptr || s_count >= MAX_DEVS) {
        return;
    }
    for (int i = 0; i < s_count; i++) {
        if (s_devs[i] == dev) return;  // 幂等: 已注册忽略
    }
    s_devs[s_count++] = dev;
}

void PowerManager::suspendAll() {
    // 逆序挂起(后注册的先收尾, 符合依赖反向)
    for (int i = s_count - 1; i >= 0; i--) {
        if (s_devs[i] != nullptr) {
            s_devs[i]->suspend();
        }
    }
}

void PowerManager::resumeAll() {
    for (int i = 0; i < s_count; i++) {
        if (s_devs[i] != nullptr) {
            s_devs[i]->resume();
        }
    }
}
