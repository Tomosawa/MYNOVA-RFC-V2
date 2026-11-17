#pragma once
#include <driver/rmt.h>
struct MockTimer;
using gptimer_handle_t=MockTimer*;
struct gptimer_alarm_event_data_t {uint64_t count_value,alarm_value;};
using gptimer_alarm_cb_t=bool(*)(gptimer_handle_t,const gptimer_alarm_event_data_t*,void*);
struct gptimer_config_t {int clk_src,direction;uint32_t resolution_hz;int intr_priority;};
struct gptimer_alarm_config_t {uint64_t alarm_count=0,reload_count=0;struct {unsigned auto_reload_on_alarm:1;} flags{};};
struct gptimer_event_callbacks_t {gptimer_alarm_cb_t on_alarm=nullptr;};
constexpr int GPTIMER_CLK_SRC_DEFAULT=0,GPTIMER_COUNT_UP=0;
struct MockTimer {bool enabled=false,started=false,armed=false;uint64_t base=0,alarm=0;gptimer_alarm_cb_t callback=nullptr;void* context=nullptr;};
inline MockTimer* mockTimer=nullptr;
inline int timerFailAt=0,timerApiCalls=0,timerAlarmCalls=0;
inline unsigned timerLateUs=0;
inline bool timerNoInterrupt=false;
inline void(*timerPollHook)()=nullptr;
inline bool timerFailure() {return ++timerApiCalls==timerFailAt;}
inline void timerAdvance(uint64_t us) {
    uint64_t end=radioClockUs+us;
    if(timerPollHook) timerPollHook();
    while(mockTimer && mockTimer->started && mockTimer->armed && !timerNoInterrupt && mockTimer->base+mockTimer->alarm<=end) {
        auto* t=mockTimer;uint64_t alarm=t->alarm;
        radioClockUs=std::max(radioClockUs,t->base+alarm)+timerLateUs;
        t->armed=false;gptimer_alarm_event_data_t event{radioClockUs-t->base,alarm};
        radioInISR=true;t->callback(t,&event,t->context);radioInISR=false;
    }
    radioClockUs=std::max(radioClockUs,end);
}
inline esp_err_t gptimer_new_timer(const gptimer_config_t* cfg,gptimer_handle_t* out) {
    if(timerFailure()) return ESP_FAIL;
    assert(!mockTimer && cfg->resolution_hz==1000000 && cfg->intr_priority==3);
    *out=mockTimer=new MockTimer;return ESP_OK;
}
inline esp_err_t gptimer_register_event_callbacks(gptimer_handle_t t,const gptimer_event_callbacks_t* cb,void* ctx) {
    if(timerFailure())return ESP_FAIL;t->callback=cb->on_alarm;t->context=ctx;return ESP_OK;
}
inline esp_err_t gptimer_enable(gptimer_handle_t t) {if(timerFailure())return ESP_FAIL;t->enabled=true;return ESP_OK;}
inline esp_err_t gptimer_set_raw_count(gptimer_handle_t t,uint64_t count) {
    if(timerFailure())return ESP_FAIL;t->base=radioClockUs-count;return ESP_OK;
}
inline esp_err_t gptimer_get_raw_count(gptimer_handle_t t,uint64_t* count) {
    if(timerFailure())return ESP_FAIL;*count=radioClockUs-t->base;return ESP_OK;
}
inline esp_err_t gptimer_set_alarm_action(gptimer_handle_t t,const gptimer_alarm_config_t* alarm) {
    ++timerAlarmCalls;if(timerFailure())return ESP_FAIL;t->alarm=alarm->alarm_count;t->armed=true;return ESP_OK;
}
inline esp_err_t gptimer_start(gptimer_handle_t t) {
    if(timerFailure())return ESP_FAIL;assert(t->enabled && SPI.marc==0x13);
    t->started=true;radioAdvanceHook=timerAdvance;return ESP_OK;
}
inline esp_err_t gptimer_stop(gptimer_handle_t t) {t->started=false;radioAdvanceHook=nullptr;return ESP_OK;}
inline esp_err_t gptimer_disable(gptimer_handle_t t) {assert(!t->started);t->enabled=false;return ESP_OK;}
inline esp_err_t gptimer_del_timer(gptimer_handle_t t) {assert(!t->enabled && !t->started);delete t;mockTimer=nullptr;return ESP_OK;}
