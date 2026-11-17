#ifdef ARDUINO_ARCH_ESP32
#include "OokRmtCapture.h"
#include <soc/soc_caps.h>

#define LOG_TAG "OOKRX"
#include "../Util/Log.h"

static_assert(SOC_RMT_SUPPORT_RX_PINGPONG, "Partial RX requires RMT ping-pong hardware");

OokRmtCapture::OokRmtCapture()
    : _pin(-1), _channel(nullptr), _enabled(false), _armed(false),
      _receiveConfig{}, _head(0), _tail(0), _queued(0), _lastSymbols(0),
      _callbacks(0), _partialCallbacks(0), _finalCallbacks(0), _dropped(0),
      _sequence(0), _lastReadSequence(0), _startsTransaction(true), _lastReadContinuous(false),
      _sequenceValid(false), _lastError(ESP_OK) {
}

OokRmtCapture::~OokRmtCapture() { end(); }

bool OokRmtCapture::check(esp_err_t error, const char* operation) {
    portENTER_CRITICAL(&_mux);
    const esp_err_t previous = _lastError;
    if (error != ESP_OK) _lastError = error;
    portEXIT_CRITICAL(&_mux);
    if (error != ESP_OK && error != previous) {
        LOG_E("[Capture] %s: %s (0x%X)", operation, esp_err_to_name(error), (unsigned)error);
    }
    return error == ESP_OK;
}

bool OokRmtCapture::begin(int pin) {
    end();
    if (pin < 0 || _channel != nullptr) return false;
    _pin = pin;
    return start();
}

void OokRmtCapture::end() { stop(); if (_channel == nullptr) _pin = -1; }

bool OokRmtCapture::start() {
    if (_pin < 0) return false;
    if (_channel != nullptr) return isRunning() || rearm();
    pinMode(_pin, INPUT);
    rmt_rx_channel_config_t config = {};
    config.gpio_num = static_cast<gpio_num_t>(_pin);
    config.clk_src = RMT_CLK_SRC_DEFAULT;
    config.resolution_hz = 1000000UL / TickUs;
    config.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
    if (!check(rmt_new_rx_channel(&config, &_channel), "rmt_new_rx_channel")) return false;
    rmt_rx_event_callbacks_t callbacks = {};
    callbacks.on_recv_done = onReceive;
    if (!check(rmt_rx_register_event_callbacks(_channel, &callbacks, this), "register callbacks") ||
        !check(rmt_enable(_channel), "rmt_enable")) {
        stop();
        return false;
    }
    _enabled = true;
    _receiveConfig = {};
    _receiveConfig.signal_range_min_ns = 3000;
    _receiveConfig.signal_range_max_ns = IdleUs * 1000UL;
    _receiveConfig.flags.en_partial_rx = true;
    LOG_I("[Capture] RMT hardware=%u symbols userBuffer=%u symbols queue=%u blocks",
           static_cast<unsigned>(config.mem_block_symbols), static_cast<unsigned>(BUFFER_SYMBOLS), QUEUE_DEPTH);
    if (!rearm()) { stop(); return false; }
    return true;
}

void OokRmtCapture::stop() {
    if (_channel != nullptr) {
        // 先停硬件及中断，再注销回调和释放资源，确保回调不再访问对象缓冲区。
        if (_enabled && !check(rmt_disable(_channel), "rmt_disable")) return;
        _enabled = false;
        rmt_rx_event_callbacks_t callbacks = {};
        if (!check(rmt_rx_register_event_callbacks(_channel, &callbacks, nullptr), "unregister callbacks")) return;
        if (!check(rmt_del_channel(_channel), "rmt_del_channel")) return;
        _channel = nullptr;
    }
    portENTER_CRITICAL(&_mux);
    _armed = false;
    _head = _tail = _queued = 0;
    _sequenceValid = false;
    portEXIT_CRITICAL(&_mux);
}

bool OokRmtCapture::restart() {
    if (!_enabled || !check(rmt_disable(_channel), "rmt_disable before retune")) return false;
    _enabled = false;
    portENTER_CRITICAL(&_mux);
    _armed = false;
    _head = _tail = _queued = 0;
    _sequenceValid = false;
    portEXIT_CRITICAL(&_mux);
    if (!check(rmt_enable(_channel), "rmt_enable after retune")) return false;
    _enabled = true;
    return rearm();
}

bool OokRmtCapture::rearm() {
    if (_channel == nullptr || !_enabled) return false;
    portENTER_CRITICAL(&_mux);
    _armed = true;  // 发起后可能立即触发回调，必须先更新状态
    _startsTransaction = true;
    portEXIT_CRITICAL(&_mux);
    const esp_err_t error = rmt_receive(_channel, _buffer, sizeof(_buffer), &_receiveConfig);
    if (!check(error, "rmt_receive(partial RX)")) {
        portENTER_CRITICAL(&_mux);
        _armed = false;
        portEXIT_CRITICAL(&_mux);
        return false;
    }
    portENTER_CRITICAL(&_mux);
    _lastError = ESP_OK;
    portEXIT_CRITICAL(&_mux);
    return true;
}

bool OokRmtCapture::isRunning() const {
    portENTER_CRITICAL(&_mux);
    const bool running = _armed;
    portEXIT_CRITICAL(&_mux);
    return _enabled && running;
}

bool IRAM_ATTR OokRmtCapture::onReceive(rmt_channel_handle_t channel,
                                          const rmt_rx_done_event_data_t* event, void* context) {
    (void)channel;
    if (context == nullptr || event == nullptr) return false;
    auto* self = static_cast<OokRmtCapture*>(context);
    portENTER_CRITICAL_ISR(&self->_mux);
    self->_callbacks++;
    if (event->flags.is_last) {
        self->_finalCallbacks++;
        self->_armed = false;
    } else {
        self->_partialCallbacks++;
    }
    if (event->num_symbols > 0 || event->flags.is_last) {
        const uint32_t sequence = ++self->_sequence;
        const bool startsTransaction = self->_startsTransaction;
        self->_startsTransaction = false;
        if ((event->num_symbols == 0 || event->received_symbols != nullptr) &&
            event->num_symbols <= BUFFER_SYMBOLS && self->_queued < QUEUE_DEPTH) {
            Block& block = self->_queue[self->_head];
            block.count = event->num_symbols;
            block.isLast = event->flags.is_last;
            block.startsTransaction = startsTransaction;
            block.sequence = sequence;
            // 回调返回后驱动会覆盖其二级缓冲区，必须在此复制，不保存 event 指针。
            for (size_t i = 0; i < block.count; i++) block.symbols[i].val = event->received_symbols[i].val;
            self->_head = (self->_head + 1) % QUEUE_DEPTH;
            self->_queued++;
        } else {
            self->_dropped++;
        }
    }
    portEXIT_CRITICAL_ISR(&self->_mux);
    return false;
}

uint16_t OokRmtCapture::read(uint16_t* durations, bool* levels, uint16_t maxCount) {
    _readBlock.isLast = false;
    _lastReadContinuous = false;
    if (durations == nullptr || maxCount == 0 || !_enabled) return 0;
    if (!isRunning()) rearm();  // 只有最后一个回调后才启动下一事务，partial 回调期间持续采集
    portENTER_CRITICAL(&_mux);
    if (_queued == 0) { portEXIT_CRITICAL(&_mux); return 0; }
    const Block& block = _queue[_tail];
    _readBlock.count = block.count;
    _readBlock.isLast = block.isLast;
    _lastReadContinuous = _sequenceValid && !block.startsTransaction && block.sequence == _lastReadSequence + 1;
    _lastReadSequence = block.sequence;
    _sequenceValid = true;
    for (size_t i = 0; i < block.count; i++) _readBlock.symbols[i].val = block.symbols[i].val;
    _tail = (_tail + 1) % QUEUE_DEPTH;
    _queued--;
    portEXIT_CRITICAL(&_mux);
    _lastSymbols = _readBlock.count;
    uint16_t count = 0;
    for (size_t i = 0; i < _readBlock.count; i++) {
        const rmt_symbol_word_t& symbol = _readBlock.symbols[i];
        const uint16_t widths[2] = {
            static_cast<uint16_t>(symbol.duration0 * TickUs),
            static_cast<uint16_t>(symbol.duration1 * TickUs)
        };
        const bool measuredLevels[2] = {symbol.level0 != 0, symbol.level1 != 0};
        for (uint8_t part = 0; part < 2; part++) {
            if (widths[part] == 0) continue;
            // 只在最后一块去掉 idle 截止段；partial 块尾部是连续波形，不可任意删除。
            if (_readBlock.isLast && widths[part] >= IdleUs - TickUs) return count;
            if (count == maxCount) {
                _sequenceValid = false;  // 下一块不能跨过本次丢弃的数据继续拼接
                _lastReadContinuous = false;
                LOG_W("[Capture] Output buffer too small; discarded block");
                return 0;
            }
            durations[count] = widths[part];
            if (levels != nullptr) levels[count] = measuredLevels[part];
            count++;
        }
    }
    return count;
}

void OokRmtCapture::printDiagnostics() const {
    portENTER_CRITICAL(&_mux);
    const uint32_t callbacks = _callbacks, partial = _partialCallbacks, finals = _finalCallbacks, dropped = _dropped;
    const uint8_t queued = _queued;
    const bool armed = _armed;
    const esp_err_t error = _lastError;
    portEXIT_CRITICAL(&_mux);
    LOG_D("[Diag] Capture=RMT partial RX, armed=%u, idle=%uus, lastSymbols=%u, callbacks=%lu partial=%lu final=%lu queued=%u dropped=%lu error=%s",
                  armed ? 1U : 0U, IdleUs, static_cast<unsigned>(_lastSymbols), (unsigned long)callbacks,
                  (unsigned long)partial, (unsigned long)finals, queued, (unsigned long)dropped,
                   esp_err_to_name(error));
}

#endif
