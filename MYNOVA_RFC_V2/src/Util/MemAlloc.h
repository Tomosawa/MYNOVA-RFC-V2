/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// MemAlloc.h —— 统一内存分配封装（规划 §五 PSRAM 策略）
// 分域规则(N8R2: 内部RAM约320KB + 2MB Octal PSRAM):
//   · 大于1KB且非DMA/非ISR/非任务栈的数据默认走 PSRAM
//   · DMA缓冲、ISR数据、任务栈、热路径小块 → 内部RAM
// 全部分配点必须判空(宏自带 failAction), 杜绝 OOM 空指针 panic。
#ifndef UTIL_MEMALLOC_H
#define UTIL_MEMALLOC_H

#include <Arduino.h>
#include <esp_heap_caps.h>

// ── 基础分配 ──────────────────────────────────────────────
// 内部 RAM 分配(8BIT 可寻址)
inline void* ramMalloc(size_t n) {
    return heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

// PSRAM 分配(无 PSRAM 板卡上返回 NULL, 由 failAction 兜底)
inline void* psramMalloc(size_t n) {
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

inline void* psramCalloc(size_t cnt, size_t size) {
    return heap_caps_calloc(cnt, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

inline void memFree(void* p) {
    heap_caps_free(p);  // 对内部RAM/PSRAM指针通吃
}

// 大块临时缓冲推荐: 优先PSRAM, 失败降级内部RAM(两者都失败返回NULL)
// 用法与malloc语句级兼容, 原有判空逻辑无需改动:
//   uint8_t* buf = (uint8_t*)anyMalloc(len);
inline void* anyMalloc(size_t n) {
    void* p = psramMalloc(n);
    if (p == nullptr) p = ramMalloc(n);
    return p;
}

// ── 判空宏(推荐用法) ─────────────────────────────────────
// 用法示例:
//   uint16_t* edges;
//   PSRAM_MALLOC_OR(edges, n * sizeof(uint16_t),
//                   { LOG_E("alloc failed"); return false; });
#define PSRAM_MALLOC_OR(ptr, size, failAction)                        \
    do {                                                              \
        (ptr) = (decltype(ptr))psramMalloc(size);                      \
        if ((ptr) == nullptr) { failAction; }                         \
    } while (0)

#define RAM_MALLOC_OR(ptr, size, failAction)                          \
    do {                                                              \
        (ptr) = (decltype(ptr))ramMalloc(size);                       \
        if ((ptr) == nullptr) { failAction; }                         \
    } while (0)

// 优先 PSRAM、失败降级内部 RAM(两者都失败才走 failAction)
// 适合"有 PSRAM 用 PSRAM, 没有也能跑"的场景
#define ANY_MALLOC_OR(ptr, size, failAction)                          \
    do {                                                              \
        (ptr) = (decltype(ptr))psramMalloc(size);                     \
        if ((ptr) == nullptr) {                                       \
            (ptr) = (decltype(ptr))ramMalloc(size);                   \
        }                                                             \
        if ((ptr) == nullptr) { failAction; }                         \
    } while (0)

#endif // UTIL_MEMALLOC_H
