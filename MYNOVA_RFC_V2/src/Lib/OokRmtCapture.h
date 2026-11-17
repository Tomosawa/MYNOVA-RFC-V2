#ifdef ARDUINO_ARCH_ESP32
/*
 * ESP-IDF 5.5 / Arduino-ESP32 3.3.5 RMT 分段接收。
 * 持续波形通过 partial RX 回调交付，ISR 只复制到有界队列；主循环负责转换/解码。
 */
#ifndef OOK_RMT_CAPTURE_H
#define OOK_RMT_CAPTURE_H

#include <Arduino.h>
#include <driver/rmt_rx.h>
#include <soc/soc_caps.h>


class OokRmtCapture {
public:
    OokRmtCapture();
    ~OokRmtCapture();
    OokRmtCapture(const OokRmtCapture&) = delete;
    OokRmtCapture& operator=(const OokRmtCapture&) = delete;

    bool begin(int pin);
    void end();
    bool restart();  // 调谐后清旧事务，复用已分配通道
    uint16_t read(uint16_t* durations, bool* levels, uint16_t maxCount);
    void printDiagnostics() const;
    bool lastReadContinuous() const { return _lastReadContinuous; }
    bool lastReadFinal() const { return _readBlock.isLast; }

private:
    // IDF 在用户缓冲区将满时才交付 partial，不是每次硬件半块中断都交付。
    static const size_t BUFFER_SYMBOLS = SOC_RMT_MEM_WORDS_PER_CHANNEL / 2;
    static const uint8_t QUEUE_DEPTH = 16;
    struct Block {
        rmt_symbol_word_t symbols[BUFFER_SYMBOLS];
        size_t count;
        bool isLast;
        bool startsTransaction;
        uint32_t sequence;
    };
    int _pin;
    rmt_channel_handle_t _channel;
    bool _enabled;
    bool _armed;
    static constexpr uint16_t IdleUs = 64000;
    static constexpr uint16_t TickUs = 2;
    rmt_receive_config_t _receiveConfig;
    rmt_symbol_word_t _buffer[BUFFER_SYMBOLS];  // 驱动的二级循环缓冲，不能直接交给解码器
    Block _queue[QUEUE_DEPTH];
    Block _readBlock{};
    uint8_t _head, _tail, _queued;
    size_t _lastSymbols;
    uint32_t _callbacks, _partialCallbacks, _finalCallbacks, _dropped;
    uint32_t _sequence, _lastReadSequence;
    bool _startsTransaction, _lastReadContinuous, _sequenceValid;
    esp_err_t _lastError;
    mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

    static bool IRAM_ATTR onReceive(rmt_channel_handle_t channel,
                                    const rmt_rx_done_event_data_t* event, void* context);
    bool start();
    void stop();
    bool isRunning() const;
    bool rearm();
    bool check(esp_err_t error, const char* operation);
};
#endif

#endif
