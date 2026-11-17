#pragma once
#include "../host_string.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <array>
#include <cassert>
using std::min;
using std::max;
#define IRAM_ATTR
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define CHANGE 2
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(p) (p)->lock()
#define portEXIT_CRITICAL(p) (p)->unlock()
#define portENTER_CRITICAL_ISR(p) (p)->lock()
#define portEXIT_CRITICAL_ISR(p) (p)->unlock()
inline uint64_t radioClockUs = 0;
inline bool modelDelayTime = true;
inline void(*radioAdvanceHook)(uint64_t) = nullptr;
inline bool radioInISR=false;
inline void advanceRadioClock(uint64_t us) { if(radioAdvanceHook) radioAdvanceHook(us); else radioClockUs+=us; }
inline std::array<int, 49> pinModes{};
inline std::array<int, 49> pinLevels{};
inline std::array<bool, 49> spiPins{};
inline std::array<void(*)(), 49> pinIsr{};
inline void(*radioSelect)(bool) = nullptr;
inline unsigned long micros() { return static_cast<uint32_t>(radioClockUs); }
inline unsigned long millis() { return static_cast<uint32_t>(radioClockUs / 1000); }
inline void delayMicroseconds(unsigned long us) { if (modelDelayTime) advanceRadioClock(us); }
inline void delay(unsigned long ms) { advanceRadioClock(ms * 1000); }
inline void noInterrupts() {}
inline void interrupts() {}
inline int digitalRead(int p) { return pinLevels[p]; }
inline int digitalPinToInterrupt(int p) { return p; }
inline void pinMode(int p, int mode) { assert(!spiPins[p]); pinModes[p] = mode; }
inline void digitalWrite(int p, int level) {
    assert(!spiPins[p]); // Arduino-ESP32 3.x rejects SPI-owned GPIO writes.
    pinLevels[p] = level;
    if (p == 38 && radioSelect) radioSelect(level == LOW);
}
inline void attachInterrupt(int p, void(*fn)(), int) { pinIsr[p] = fn; }
inline void detachInterrupt(int p) { pinIsr[p] = nullptr; }
