#pragma once
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/gptimer.h>

// 队列/状态放内部RAM；文件读取留在worker，ISR只出队、写GPIO、安排下个绝对时间。
class RawGpioStream {
public:
    static constexpr uint32_t Capacity = 2048;
    struct Status {
        bool done, underrun, late, timerError;
        uint32_t emitted, maxLateUs;
    };
    explicit RawGpioStream(gpio_num_t output) : pin(output) {}
    uint32_t freeSpace() const {
        portENTER_CRITICAL(&mux);
        uint32_t n = Capacity - (head - tail);
        portEXIT_CRITICAL(&mux);
        return n;
    }
    void push(int32_t pulse) {
        portENTER_CRITICAL(&mux);
        pulses[head++ % Capacity] = pulse;
        portEXIT_CRITICAL(&mux);
    }
    void finish() { portENTER_CRITICAL(&mux); ended=true; portEXIT_CRITICAL(&mux); }
    Status snapshot() const {
        portENTER_CRITICAL(&mux);
        Status s{done,underrun,late,timerError,emitted,maxLateUs};
        portEXIT_CRITICAL(&mux);
        return s;
    }
    static bool IRAM_ATTR alarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t* event, void* context);
private:
    bool IRAM_ATTR onAlarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t* event);
    gpio_num_t pin;
    int32_t pulses[Capacity];
    uint32_t head=0,tail=0,emitted=0,maxLateUs=0,previousDuration=0;
    bool ended=false,done=false,underrun=false,late=false,timerError=false;
    mutable portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
};
