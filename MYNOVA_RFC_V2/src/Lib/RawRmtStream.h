#pragma once
#include <Arduino.h>
#include <cstring>

// 单生产者（文件任务）/单消费者（RMT ISR）。整个对象必须放内部RAM。
// 短临界区保护索引；不在ISR中使用可能通过库锁实现的std::atomic或读取文件。
class RawRmtStream {
public:
    static constexpr uint32_t Capacity = 2048;
    static_assert(sizeof(rmt_data_t) == sizeof(rmt_symbol_word_t));
    uint32_t freeSpace() const {
        portENTER_CRITICAL(&mux);
        uint32_t n = Capacity - (head - tail);
        portEXIT_CRITICAL(&mux);
        return n;
    }
    void push(const rmt_data_t& item) {
        portENTER_CRITICAL(&mux);
        data[head % Capacity] = item;
        ++head;
        portEXIT_CRITICAL(&mux);
    }
    void finish() { portENTER_CRITICAL(&mux); ended = true; portEXIT_CRITICAL(&mux); }
    bool isFinished() const { portENTER_CRITICAL(&mux); bool v=ended; portEXIT_CRITICAL(&mux); return v; }
    bool hasUnderrun() const { portENTER_CRITICAL(&mux); bool v=underrun; portEXIT_CRITICAL(&mux); return v; }
    void markUnderrun() { portENTER_CRITICAL(&mux); underrun=true; portEXIT_CRITICAL(&mux); }
    size_t IRAM_ATTR consume(rmt_symbol_word_t* out, size_t capacity, bool* done) {
        // ESP32的ISR临界区实现也可用于RMT在任务上下文进行的首次预编码。
        portENTER_CRITICAL_ISR(&mux);
        size_t n = (head - tail < capacity) ? head - tail : capacity;
        for (size_t i = 0; i < n; ++i) memcpy(out + i, &data[(tail + i) % Capacity], sizeof(rmt_data_t));
        tail += n;
        *done = ended && tail == head;
        if (!n && !*done) { underrun = true; *done = true; }
        portEXIT_CRITICAL_ISR(&mux);
        return n;
    }
    uint32_t consumed() const { portENTER_CRITICAL(&mux); auto n=tail; portEXIT_CRITICAL(&mux); return n; }
private:
    rmt_data_t data[Capacity];
    uint32_t head=0, tail=0;
    bool ended=false, underrun=false;
    mutable portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
};
