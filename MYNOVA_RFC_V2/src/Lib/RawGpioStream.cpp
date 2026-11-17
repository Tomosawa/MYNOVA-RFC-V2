#include "RawGpioStream.h"

bool IRAM_ATTR RawGpioStream::alarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t* event, void* context) {
    return static_cast<RawGpioStream*>(context)->onAlarm(timer,event);
}

bool IRAM_ATTR RawGpioStream::onAlarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t* event) {
    int32_t pulse = 0;
    portENTER_CRITICAL_ISR(&mux);
    bool empty = tail == head;
    if (!empty) pulse = pulses[tail++ % Capacity];
    else { done=true; underrun=!ended; }
    portEXIT_CRITICAL_ISR(&mux);
    if (empty) { gpio_set_level(pin,0); return false; }

    uint64_t now = 0;
    bool failed = gptimer_get_raw_count(timer,&now) != ESP_OK;
    uint32_t lateUs = now > event->alarm_value ? (uint32_t)(now-event->alarm_value) : 0;
    // 过迟的边沿不能宣称保真。短段容许1/4段宽，长段最多20us；初始触发20us。
    uint32_t tolerance = previousDuration ? previousDuration/4 : 20;
    if (tolerance > 20) tolerance=20;
    if (tolerance < 2) tolerance=2;
    portENTER_CRITICAL_ISR(&mux);
    if (lateUs > maxLateUs) maxLateUs=lateUs;
    if (failed || lateUs > tolerance) { done=true; timerError=failed; late=lateUs>tolerance; }
    bool stop=done;
    portEXIT_CRITICAL_ISR(&mux);
    if (stop) { gpio_set_level(pin,0); return false; }

    int64_t v=pulse;
    uint32_t duration=(uint32_t)(v<0 ? -v : v);
    failed = gpio_set_level(pin,pulse>0) != ESP_OK;
    previousDuration=duration;
    gptimer_alarm_config_t next = {};
    next.alarm_count=event->alarm_value+duration; // 绝对时刻，避免逐段累加ISR执行耗时
    if (!failed) failed=gptimer_set_alarm_action(timer,&next)!=ESP_OK;
    portENTER_CRITICAL_ISR(&mux);
    ++emitted;
    if (failed) { done=true;timerError=true; }
    portEXIT_CRITICAL_ISR(&mux);
    if (failed) gpio_set_level(pin,0);
    return false;
}
