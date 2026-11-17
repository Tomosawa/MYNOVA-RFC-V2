#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
using std::min;
using std::max;
#define IRAM_ATTR
#define HIGH 1
#define LOW 0
#define INPUT 0
#define CHANGE 2
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(p) (p)->lock()
#define portEXIT_CRITICAL(p) (p)->unlock()
#define portENTER_CRITICAL_ISR(p) (p)->lock()
#define portEXIT_CRITICAL_ISR(p) (p)->unlock()
inline std::atomic<unsigned long> testMicros{0};
inline int testLevel = 0;
inline auto testEpoch = std::chrono::steady_clock::now();
inline unsigned long micros() { return testMicros; }
inline unsigned long millis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - testEpoch).count();
}
inline void noInterrupts() {}
inline void interrupts() {}
inline int digitalRead(int) { return testLevel; }
inline int digitalPinToInterrupt(int p) { return p; }
inline void pinMode(int, int) {}
inline void attachInterrupt(int, void(*)(), int) {}
inline void detachInterrupt(int) {}
