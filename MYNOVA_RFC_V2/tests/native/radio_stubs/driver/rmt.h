#pragma once
#include <Arduino.h>
#include <vector>
struct rmt_data_t { uint32_t duration0:15; uint32_t level0:1; uint32_t duration1:15; uint32_t level1:1; };
static_assert(sizeof(rmt_data_t)==4);
using rmt_symbol_word_t = rmt_data_t;
#define RMT_TX_MODE 1
#define RMT_MEM_NUM_BLOCKS_2 2
#define RMT_WAIT_FOR_EVER 0xffffffff
inline bool rmtActive = false, rmtNeverComplete = false, rmtInitFails = false;
inline int rmtRejectWrite = 0, rmtWriteCalls = 0, rmtDeinitCalls = 0;
inline uint64_t rmtEndUs = 0;
inline const rmt_data_t* rmtPending = nullptr;
inline uint32_t rmtPendingCount = 0;
inline void(*rmtPollHook)() = nullptr;
inline std::vector<std::vector<rmt_data_t>> rmtWrites;
using esp_err_t = int;
constexpr esp_err_t ESP_OK=0, ESP_ERR_TIMEOUT=1, ESP_FAIL=2;
using rmt_channel_handle_t = void*;
using rmt_encoder_handle_t = void*;
constexpr int RMT_CLK_SRC_DEFAULT=0;
struct rmt_tx_channel_config_t {
    int gpio_num=0, clk_src=0, resolution_hz=0, mem_block_symbols=0, trans_queue_depth=0;
};
struct rmt_copy_encoder_config_t {};
struct rmt_transmit_config_t {};
using rmt_encode_simple_cb_t = size_t(*)(const void*,size_t,size_t,size_t,rmt_symbol_word_t*,bool*,void*);
struct rmt_simple_encoder_config_t {rmt_encode_simple_cb_t callback=nullptr;void* arg=nullptr;size_t min_chunk_size=0;};
inline rmt_encode_simple_cb_t rmtSimpleCallback=nullptr;
inline void* rmtSimpleContext=nullptr;
inline const uint8_t* rmtStreamMarker=nullptr;
inline bool rmtStreamDone=false;
inline size_t rmtStreamEncoded=0;
inline bool rmtEncoderActive=false;
inline esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t* cfg,rmt_channel_handle_t* out) {
    if (rmtInitFails) return ESP_FAIL;
    assert(cfg->resolution_hz==1000000 && cfg->mem_block_symbols==96);
    rmtActive=true; pinModes[cfg->gpio_num]=OUTPUT; *out=(void*)1; return ESP_OK;
}
inline esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t*,rmt_encoder_handle_t* out) {
    rmtEncoderActive=true;rmtSimpleCallback=nullptr; *out=(void*)2; return ESP_OK;
}
inline esp_err_t rmt_new_simple_encoder(const rmt_simple_encoder_config_t* cfg,rmt_encoder_handle_t* out) {
    assert(cfg->min_chunk_size==1);
    rmtSimpleCallback=cfg->callback;rmtSimpleContext=cfg->arg;
    rmtEncoderActive=true;*out=(void*)2;return ESP_OK;
}
inline void rmtModelRefill() {
    rmt_symbol_word_t symbols[48]; bool done=false;
    size_t n=rmtSimpleCallback(rmtStreamMarker,1,rmtStreamEncoded,48,symbols,&done,rmtSimpleContext);
    rmtWrites.back().insert(rmtWrites.back().end(),symbols,symbols+n);
    rmtStreamEncoded+=n;rmtStreamDone=done;
    for(size_t i=0;i<n;++i) rmtEndUs+=symbols[i].duration0+symbols[i].duration1;
}
inline esp_err_t rmt_enable(rmt_channel_handle_t) {return ESP_OK;}
inline esp_err_t rmt_transmit(rmt_channel_handle_t, rmt_encoder_handle_t, const void* payload,
                              size_t bytes, const rmt_transmit_config_t*) {
    assert(SPI.marc==0x13); // GPIO output is only valid after the CC1101 reaches TX.
    if(rmtSimpleCallback) {
        assert(rmtActive && rmtEncoderActive && bytes==1 && !rmtStreamMarker);
        if(++rmtWriteCalls==rmtRejectWrite) return ESP_FAIL;
        rmtStreamMarker=static_cast<const uint8_t*>(payload);rmtStreamDone=false;rmtStreamEncoded=0;
        rmtWrites.emplace_back();rmtEndUs=radioClockUs;rmtModelRefill();return ESP_OK;
    }
    auto data=static_cast<const rmt_data_t*>(payload); uint32_t count=bytes/sizeof(rmt_data_t);
    assert(rmtActive && count && !rmtPending && rmtEncoderActive);
    if (++rmtWriteCalls == rmtRejectWrite) return ESP_FAIL;
    rmtWrites.emplace_back(data, data + count);
    rmtPending = data; rmtPendingCount = count;
    rmtEndUs = radioClockUs;
    for (uint32_t i = 0; i < count; ++i) {
        assert(data[i].duration0 > 0 && data[i].duration0 <= 32767);
        assert(data[i].duration1 > 0 && data[i].duration1 <= 32767);
        rmtEndUs += data[i].duration0 + data[i].duration1;
    }
    return ESP_OK;
}
inline esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t,int timeout) {
    assert(timeout>0 && timeout<=5);
    if(rmtPollHook) rmtPollHook();
    radioClockUs += timeout*1000;
    if(rmtNeverComplete) return ESP_ERR_TIMEOUT;
    if(rmtStreamMarker) {
        while(radioClockUs>=rmtEndUs && !rmtStreamDone) rmtModelRefill();
        if(radioClockUs<rmtEndUs) return ESP_ERR_TIMEOUT;
        rmtStreamMarker=nullptr;return ESP_OK;
    }
    if(radioClockUs<rmtEndUs) return ESP_ERR_TIMEOUT;
    rmtPending=nullptr;return ESP_OK;
}
inline esp_err_t rmt_disable(rmt_channel_handle_t) {
    assert(SPI.marc == 1); // RF carrier off before stop and release.
    if (rmtPending) { // ASan verifies active payload still alive when stopped.
        volatile auto duration = rmtPending[rmtPendingCount - 1].duration1;
        (void)duration;
    }
    if(rmtStreamMarker) {volatile auto marker=*rmtStreamMarker;(void)marker;}
    rmtStreamMarker=nullptr;
    rmtPending=nullptr; rmtActive=false; ++rmtDeinitCalls; return ESP_OK;
}
inline esp_err_t rmt_del_encoder(rmt_encoder_handle_t) {
    assert(!rmtPending && !rmtStreamMarker); rmtEncoderActive=false;rmtSimpleCallback=nullptr; return ESP_OK;
}
inline esp_err_t rmt_del_channel(rmt_channel_handle_t) {assert(!rmtActive && !rmtEncoderActive);return ESP_OK;}
