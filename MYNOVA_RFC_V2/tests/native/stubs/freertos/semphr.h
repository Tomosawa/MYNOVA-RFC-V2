#pragma once
#include "Arduino.h"
#include <condition_variable>
struct TestSemaphore { std::mutex mux; std::condition_variable ready; bool given = false; };
using SemaphoreHandle_t = TestSemaphore*;
#define portMAX_DELAY 0xffffffffu
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
inline bool testTaskCreateFailure = false;
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return new TestSemaphore; }
inline void vSemaphoreDelete(SemaphoreHandle_t s) { delete s; }
inline int xSemaphoreGive(SemaphoreHandle_t s) {
    std::lock_guard<std::mutex> guard(s->mux);
    s->given = true;
    s->ready.notify_one();
    return 1;
}
inline int xSemaphoreTake(SemaphoreHandle_t s, unsigned) {
    std::unique_lock<std::mutex> lock(s->mux);
    s->ready.wait(lock, [&]{ return s->given; });
    s->given = false;
    return 1;
}
inline int xTaskCreatePinnedToCore(void(*task)(void*), const char*, unsigned,
        void* context, unsigned, void*, unsigned) {
    if (testTaskCreateFailure) return 0;
    std::thread([=]{ task(context); }).detach();
    return 1;
}
inline void vTaskDelay(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
inline void vTaskDelete(void*) {}
