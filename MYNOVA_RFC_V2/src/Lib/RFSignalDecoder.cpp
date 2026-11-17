/*
  RF Signal Decoder Implementation - Improved Version

  普通接收：连续段队列 -> 注册协议机 -> 真实帧锚点及重复确认。
  RAW：同一段队列 -> 后台录制；两种模式互斥。
  通用时序分析函数保留供辅助分析，不参与现场协议确认。
*/

#include "RFSignalDecoder.h"
#ifdef ARDUINO_ARCH_ESP32
#include <driver/gpio.h>
#endif
#include "../Util/Log.h" // 分级日志

#define LOG_TAG "DECOD"

// ──────────────────────────────────────────────────────────────────────────────
// 协议数据库  (short/long 单位 us；syncMin/syncMax=0 表示不强制匹配同步头长度)
// ──────────────────────────────────────────────────────────────────────────────
const ProtocolProfile RFSignalDecoder::PROTOCOLS[] = {

    // ── PT2262 系 (24-bit PWM, 长:短 ≈ 3:1, 同步头 ≈ 31T) ──────────────────
    {"PT2262",      1,  280, 480,  840, 1350, 2800, 4500,  24, ENCODING_PWM},
    {"PT2260",      2,  280, 480,  840, 1350, 2800, 4500,  12, ENCODING_PWM},
    {"PT2264",      3,  280, 480,  840, 1350, 2800, 4500,  18, ENCODING_PWM},
    {"SC5262",      4,  280, 480,  840, 1350, 2800, 4500,  24, ENCODING_PWM},
    {"HX2262",      5,  280, 480,  840, 1350, 2800, 4500,  24, ENCODING_PWM},
    {"SC5272",      6,  280, 480,  840, 1350, 2800, 4500,  12, ENCODING_PWM},

    // ── EV1527 系 (24-bit 学习码，同步头 = 1T+31T，比 PT2262 略快) ──────────
    {"EV1527",     10,  250, 450,  750, 1100, 250,  550,   24, ENCODING_PWM},
    {"RT1527",     11,  250, 450,  750, 1100, 250,  550,   24, ENCODING_PWM},
    {"FP527",      12,  250, 450,  750, 1100, 250,  550,   24, ENCODING_PWM},
    {"HS1527",     13,  250, 450,  750, 1100, 250,  550,   24, ENCODING_PWM},
    {"EV1527F",    14,  180, 380,  550,  900, 180,  420,   24, ENCODING_PWM},  // 快速变体

    // ── Holtek 系 ──────────────────────────────────────────────────────────
    {"HT12E",      20,  280, 520,  840, 1400, 8000,14500,  12, ENCODING_PWM},
    {"HT6P20B",    21,  300, 520,  840, 1200, 4000, 7000,  24, ENCODING_PWM},
    {"HT6P20C",    22,  280, 480,  840, 1200, 4000, 7000,  28, ENCODING_PWM},
    {"HT12D",      23,  280, 520,  840, 1400, 8000,14500,   8, ENCODING_PWM},

    // ── 中国通用无线遥控芯片 ─────────────────────────────────────────────────
    {"SMC5326",    30,  250, 450,  750, 1100, 5500,12000,  25, ENCODING_PWM},
    {"WL101",      31,  150, 350,  480,  820, 6500,13000,  24, ENCODING_PWM},
    {"BL999",      32,  280, 480,  840, 1200, 8500,14000,  24, ENCODING_PWM},
    {"AB1527",     33,  250, 450,  750, 1100, 250,  600,   24, ENCODING_PWM},

    // ── 欧洲家居自动化 ───────────────────────────────────────────────────────
    {"INTERTECHNO",40,  200, 380,  620, 1000, 2000, 3200,  12, ENCODING_PWM},
    {"NEXA",       41,  150, 350,  680, 1150, 2200, 3000,  32, ENCODING_PWM},
    {"DOOYA",      42,  250, 470,  580,  950, 3500, 5500,  40, ENCODING_PWM},
    {"SOMFY_RTS",  43,  280, 720,  950, 1500, 1800, 3200,  80, ENCODING_PWM},

    // ── 安防/报警 ────────────────────────────────────────────────────────────
    {"KERUI",      50,  300, 580,  880, 1450, 8500,14500,  24, ENCODING_PWM},
    {"DSC",        51,  380, 620,  730, 1100,12000,19000,  36, ENCODING_PWM},
    {"VISONIC",    52,  350, 580,  880, 1280,    0,    0,  32, ENCODING_MANCHESTER},

    // ── 车库门/大门 ──────────────────────────────────────────────────────────
    {"CAME",       60,  280, 500,  840, 1250,    0,    0,  12, ENCODING_PPM},
    {"NICE",       61,  280, 500,  840, 1250,    0,    0,  12, ENCODING_PPM},
    {"BFT",        62,  180, 400,  520,  880,    0,    0,  64, ENCODING_MANCHESTER},
    {"FAAC",       63,  280, 500,  840, 1250,    0,    0,   8, ENCODING_PPM},

    // ── 汽车/门禁（Keeloq 类，仅匹配时序；滚动码无法完整解码，记录原始数据）──
    {"KEELOQ",     70,  180, 340,  420,  680, 6500,12000,  66, ENCODING_PWM},

    // ── 气象传感器（Manchester 编码）──────────────────────────────────────────
    {"OREGON_V1",  80,  380, 640,  880, 1250,    0,    0,  32, ENCODING_MANCHESTER},

    // ── 兜底：原始脉冲 ────────────────────────────────────────────────────────
    {"RAW",       255,    0,65535,   0,65535,    0,65535,   0, ENCODING_RAW}
};

const uint8_t RFSignalDecoder::PROTOCOL_COUNT =
    sizeof(PROTOCOLS) / sizeof(PROTOCOLS[0]);

// ──────────────────────────────────────────────────────────────────────────────

RFSignalDecoder* RFSignalDecoder::_instance = nullptr;

RFSignalDecoder::RFSignalDecoder() {
    _gdo2Pin = -1;
    _instance = this;
    _lastPulseTime = 0;
    _lastInterruptTime = 0;

    _minPulseWidth = 30;      // 对齐Flipper worker，保留33/66us等有效短段
    _maxPulseWidth = 50000;   // 50ms  — 支持 31T×1600us 同步头
    _packetGap     = 15000;   // 15ms  — 初始包间隔，动态调整
    _verboseDebug  = false;   // 详细脉冲打印开关
    _invert        = false;   // 默认 ISR 取反后即真实电平，反极性命中后自动学习

    // RAW 录制模式
    _rawMode = false;
    _rawHead = _rawTail = 0;
    _rawAccumUs = 0;
    _rawOverrun = false;
    _rawProduced = 0;
    _rawMinUs = RAW_MIN_US;

    _totalSignals   = 0;
    _decodedSignals = 0;
    _failedDecodes  = 0;

    // Flipper 式协议机绑定（与 RFProtocolDecoders.h 的 Te 实测值保持一致）
    _protoPT2262.rebind(&PROTO_PRINCETON, "PT2262", 1);
    _protoEV1527.rebind(&PROTO_EV1527, "EV1527", 10);
    _protoSMC5326.rebind(&PROTO_SMC5326, "SMC5326", 30);

    // CameStyle 新增协议（CAME / Nice FLO / Holtek）在头文件类内初始化绑定常量

    // 注册到全局协议注册中心（幂等：重建注册表指向本实例协议机）
    registerProtocols();
    reset();

    memset(&_decodedSignal, 0, sizeof(_decodedSignal));
    memset(&_lastSignal,    0, sizeof(_lastSignal));
}

// 注册全部协议机到 RFProtocolRegistry —— 增删协议只改这里与头文件成员
void RFSignalDecoder::registerProtocols() {
    RFProtocolRegistry& reg = RFProtocolRegistry::instance();
    reg.clear();
    reg.add(&_protoPT2262);
    reg.add(&_protoEV1527);
    reg.add(&_protoSMC5326);
    reg.add(&_protoCAME);
    reg.add(&_protoNiceFlo);
    reg.add(&_protoHoltek);
    reg.add(&_protoPWM);
}

void RFSignalDecoder::begin(int gdo0Pin, int gdo2Pin) {
    end();
    _gdo0Pin = gdo0Pin;
    _gdo2Pin = gdo2Pin;
    _traceDumps = 0;
    _lastTraceDumpSegmentCount = 0;
    _frameTraceDumps = 0;
    _edgeTraceDumps = 0;
    _lastEdgeDumpTotal = 0;

    pinMode(_gdo0Pin, INPUT);
    pinMode(_gdo2Pin, INPUT);

    attachInterrupt(digitalPinToInterrupt(_gdo2Pin),
                    RFSignalDecoder::handleInterrupt,
                    CHANGE);

    LOG_I("[Decoder] Init: pin=%d, minPulse=%dus, maxFrameGap=1000000us",
          _gdo2Pin, _minPulseWidth);
}

void RFSignalDecoder::end() {
    stopOokCapture();
    if (_gdo2Pin >= 0) detachInterrupt(digitalPinToInterrupt(_gdo2Pin));
    _gdo2Pin = -1;
}

void RFSignalDecoder::stopOokCapture() {
#ifdef ARDUINO_ARCH_ESP32
    if (_ookRmtActive) {
        _ookCapture.end();
        _ookRmtActive = false;
    }
#endif
}

void RFSignalDecoder::beginOokCapture(int pin) {
#ifdef ARDUINO_ARCH_ESP32
    _ookFrame.reset();
    _ookRmtActive = _ookCapture.begin(pin);
    if (_ookRmtActive) LOG_I("[RX.profile] OOK RMT GPIO%d", pin);
    else LOG_W("[RX.profile] RMT unavailable; using GPIO%d RX", _gdo2Pin);
#else
    (void)pin;
#endif
}

void RFSignalDecoder::restartOokCapture() {
#ifdef ARDUINO_ARCH_ESP32
    if (_ookRmtActive && !_ookCapture.restart()) {
        stopOokCapture();
        LOG_W("[RX.profile] RMT restart failed; using GPIO%d RX", _gdo2Pin);
    }
#endif
}

#ifdef ARDUINO_ARCH_ESP32
bool RFSignalDecoder::readOokCapture() {
    // RMT 回调只排队，任务消费有界数量，GPIO40 旧协议仍可处理其它格式。
    uint16_t widths[48];
    bool levels[48];
    for (uint8_t i = 0; i < 16; ++i) {
        const uint16_t count = _ookCapture.read(widths, levels, 48);
        const bool final = _ookCapture.lastReadFinal();
        if (!count && !final) break;
        OokFrameDecoder::Frame frame;
        const uint8_t repeats = _ookFrame.feed(widths, levels, count,
                                              _ookCapture.lastReadContinuous(), final, frame);
        if (frame.bits) _lastFrameCandidateAt = millis();
        if (repeats < 2) continue;
        _decodedSignal = {};
        _decodedSignal.timestamp = millis();
        _decodedSignal.bitCount = frame.bits;
        _decodedSignal.shortPulse = frame.shortUs;
        _decodedSignal.longPulse = frame.longUs;
        _decodedSignal.syncPulse = frame.syncUs;
        _decodedSignal.guardDurationUs = frame.syncUs;
        _decodedSignal.packetGap = frame.syncUs;
        memcpy(_decodedSignal.data, frame.data, sizeof(frame.data));
        _decodedSignal.repeatCount = repeats;
        _decodedSignal.protocolValidated = true;
        _decodedSignal.encodingType = ENCODING_PWM;
        _decodedSignal.signalType = SIGNAL_FIXED_CODE;
        _decodedSignal.protocolId = 200;
        strcpy(_decodedSignal.protocolName, "PWM");
        pulsesToHex(_decodedSignal.data, frame.bits, _decodedSignal.hexString);
        pulsesToBinary(_decodedSignal.data, frame.bits, _decodedSignal.binaryString);
        _lastSignal = _decodedSignal;
        ++_decodedSignals;
        return true;
    }
    return false;
}
#endif

uint16_t RFSignalDecoder::edgeSnapshot(RawSeg* out, uint16_t capacity,
                                     uint32_t& total, uint32_t& glitches) {
    portENTER_CRITICAL(&_rawMux);
    total = _edgeTraceTotal;
    glitches = _rxGlitches;
    uint16_t count = out ? min(capacity, _edgeTraceCount) : 0;
    uint16_t first = (_edgeTraceHead + RX_TRACE_SIZE - count) % RX_TRACE_SIZE;
    for (uint16_t i = 0; i < count; i++) out[i] = _edgeTrace[(first + i) % RX_TRACE_SIZE];
    portEXIT_CRITICAL(&_rawMux);
    return count;
}

// ──────────────────────────────────────────────────────────────────────────────
// RAW 连续录制模式
// ──────────────────────────────────────────────────────────────────────────────
void RFSignalDecoder::enableRawMode(bool on, uint16_t minUs) {
    if (on) rawReset();
    portENTER_CRITICAL(&_rawMux);
    if (!on && _rawMode && _rawPendingValid) {
        rawPushIsr(_rawPendingLevel, _rawAccumUs);
        _rawPendingValid = false;
        _rawAccumUs = 0;
    }
    _rawMode = on;
    if (minUs > 0) _rawMinUs = minUs;
    portEXIT_CRITICAL(&_rawMux);
}

void RFSignalDecoder::flushRawTail(bool closingGate) {
    if (!_rawPendingValid) return;
    rawPushIsr(_rawPendingLevel, _rawAccumUs);
    if (!_rawMode || !_rawGate || !_rawStarted) return;

    // 没有下一次上升沿时，同步低电平仍在持续。记录到停止或关门限的实测时刻。
    const uint32_t elapsed = static_cast<uint32_t>(micros() - _lastPulseTime);
    const bool current = digitalRead(_gdo2Pin) == HIGH;
    if (elapsed >= _rawMinUs && current != _rawPendingLevel && (!closingGate || !current)) {
        rawPushIsr(current, elapsed);
    }
}

void RFSignalDecoder::finishRawCapture() {
    portENTER_CRITICAL(&_rawMux);
    if (_rawMode) flushRawTail(false);
    _rawMode = false;
    _rawPendingValid = false;
    _rawAccumUs = 0;
    portEXIT_CRITICAL(&_rawMux);
}

void RFSignalDecoder::rawReset() {
    portENTER_CRITICAL(&_rawMux);
    _rawHead = 0;
    _rawTail = 0;
    _rawAccumUs = 0;
    _rawOverrun = false;
    _rawProduced = 0;
    _rawEdges = 0;
    _rawPendingValid = false;
    _rawStarted = false;
    _rawGate = true;
    _lastPulseTime = micros();
    portEXIT_CRITICAL(&_rawMux);
}

void RFSignalDecoder::rawSetGate(bool open) {
    portENTER_CRITICAL(&_rawMux);
    if (_rawGate != open) {
        // 关闭时保留最后一个已接受的段；打开时丢掉跨越静默的首段。
        if (!open) flushRawTail(true);
        _rawPendingValid = false;
        _rawAccumUs = 0;
        _rawStarted = false;
        _rawGate = open;
    }
    portEXIT_CRITICAL(&_rawMux);
}

void IRAM_ATTR RFSignalDecoder::rawPushIsr(bool level, uint32_t durUs) {
    if (!_rawMode) {
        // 普通RX保留完整同步间隔；复用4字节槽内原来的padding存时长高8位。
        // 长间隔不代表队列丢段，不能伪造成60000us复位标记。
        constexpr uint32_t MAX_RX_SEG_US = 0xFFFFFF;
        durUs = min(durUs, MAX_RX_SEG_US);
        if (!durUs) return;
        if (_rawHead != _rawTail) {
            uint16_t prev = (_rawHead + RAW_RING_SIZE - 1) % RAW_RING_SIZE;
            uint32_t previous = ((uint32_t)_rawRing[prev].durationHigh << 16) | _rawRing[prev].durUs;
            if (_rawRing[prev].level == (uint8_t)level && previous + durUs <= MAX_RX_SEG_US) {
                uint32_t sum = previous + durUs;
                _rawRing[prev].durUs = (uint16_t)sum;
                _rawRing[prev].durationHigh = (uint8_t)(sum >> 16);
                return;
            }
        }
        uint16_t next = (_rawHead + 1) % RAW_RING_SIZE;
        if (next == _rawTail) { _rawOverrun = true; return; }
        _rawRing[_rawHead].durUs = (uint16_t)durUs;
        _rawRing[_rawHead].durationHigh = (uint8_t)(durUs >> 16);
        _rawRing[_rawHead].level = level ? 1 : 0;
        _rawHead = next;
        _rawProduced++;
        return;
    }
    // RAW文件接口保持16位段，长段拆分，不改变录制/回放格式。
    while (durUs > 0) {
        uint16_t d = (durUs > RAW_SEG_MAX_US) ? RAW_SEG_MAX_US : (uint16_t)durUs;
        bool merged = false;
        if (_rawHead != _rawTail) {
            uint16_t prev = (uint16_t)((_rawHead + RAW_RING_SIZE - 1) % RAW_RING_SIZE);
            if (_rawRing[prev].level == (uint8_t)level &&
                (uint32_t)_rawRing[prev].durUs + d <= RAW_SEG_MAX_US) {
                _rawRing[prev].durUs = (uint16_t)(_rawRing[prev].durUs + d);
                merged = true;
            }
        }
        if (!merged) {
            uint16_t next = (uint16_t)((_rawHead + 1) % RAW_RING_SIZE);
            if (next == _rawTail) {
                _rawOverrun = true;   // 环形满：丢新段，等消费侧排空
                return;
            }
            _rawRing[_rawHead].durUs = d;
            _rawRing[_rawHead].durationHigh = 0;
            _rawRing[_rawHead].level = level ? 1 : 0;
            _rawHead = next;
            _rawProduced++;
        }
        durUs -= d;
    }
}

uint16_t RFSignalDecoder::rawDrain(RawSeg* out, uint16_t maxCount) {
    if (out == nullptr || maxCount == 0) return 0;
    uint16_t n = 0;
    portENTER_CRITICAL(&_rawMux);
    while (n < maxCount && _rawTail != _rawHead) {
        out[n].durUs = _rawRing[_rawTail].durUs;
        out[n].level = _rawRing[_rawTail].level;
        out[n].durationHigh = 0;
        _rawTail = (uint16_t)((_rawTail + 1) % RAW_RING_SIZE);
        n++;
    }
    portEXIT_CRITICAL(&_rawMux);
    return n;
}

bool RFSignalDecoder::rawTakeOverrun() {
    portENTER_CRITICAL(&_rawMux);
    bool o = _rawOverrun;
    _rawOverrun = false;
    portEXIT_CRITICAL(&_rawMux);
    return o;
}

void IRAM_ATTR RFSignalDecoder::handleInterrupt() {
    if (_instance) _instance->processInterrupt();
}

void IRAM_ATTR RFSignalDecoder::processInterrupt() {
    unsigned long now = micros();
#ifdef ARDUINO_ARCH_ESP32
    // IRQ入口即取电平，避免在临界区锁等待后才采样；跳过Arduino引脚所有权查询。
    bool segLevel = gpio_get_level((gpio_num_t)_gdo2Pin) != HIGH;
#else
    bool segLevel = digitalRead(_gdo2Pin) != HIGH;
#endif
    portENTER_CRITICAL_ISR(&_rawMux);
    _edgeCount++;
    unsigned long duration = now - _lastPulseTime;
    _lastPulseTime = now;   // 所有分支统一推进时间戳（原实现每分支各自赋值）

    // Flipper worker语义：短毛刺并入前一段；遇到不同电平的有效段才提交。
    // 不能把短毛刺并入下一段后直接用下一段电平，否则会改变波形极性。
    if (_rawMode) {
        _rawEdges++;
        if (_rawGate) {
            if (!_rawStarted) {
                _rawStarted = true;  // 首边沿之前是等待时间，不是遥控脉冲
            } else {
                if (_rawPendingValid &&
                    (duration < _rawMinUs || segLevel == _rawPendingLevel)) {
                    _rawAccumUs += duration;
                } else if (duration >= _rawMinUs) {
                    if (_rawPendingValid) rawPushIsr(_rawPendingLevel, _rawAccumUs);
                    _rawPendingLevel = segLevel;
                    _rawPendingValid = true;
                    _rawAccumUs = duration;
                }
            }
        }
        portEXIT_CRITICAL_ISR(&_rawMux);
        return;
    }
    // 普通接收同样连续入队。消费任务尚在解码上一帧时也保留下一帧的边沿。
    // 与 RAW 共用已有环形缓冲，避免额外占用低水位时紧张的内部 RAM。
    if (!_rxStarted) {
        _rxStarted = true; // 首边沿之前的等待不能作为虚构帧头
    } else {
        uint32_t captured = min((uint32_t)duration, (uint32_t)0xFFFFFF);
        _edgeTrace[_edgeTraceHead] = {(uint16_t)captured, (uint8_t)segLevel,
                                     (uint8_t)(captured >> 16)};
        _edgeTraceHead = (_edgeTraceHead + 1) % RX_TRACE_SIZE;
        if (_edgeTraceCount < RX_TRACE_SIZE) _edgeTraceCount++;
        _edgeTraceTotal++;
        if (duration < _minPulseWidth) _rxGlitches++;
        // 短毛刺并入原电平，保留其时间；后续同电平段继续粘合。
        if (_rxPendingValid &&
            (duration < _minPulseWidth || segLevel == _rxPendingLevel)) {
            _rxAccumUs += duration;
        } else if (duration >= _minPulseWidth) {
            if (_rxPendingValid) rawPushIsr(_rxPendingLevel, _rxAccumUs);
            _rxPendingLevel = segLevel;
            _rxPendingValid = true;
            _rxAccumUs = duration;
        }
        // 不能凭持续时间长就提前提交 Guard：间隔内的短毛刺仍须并回原段。
        // 等到有效的反向段才提交（与 Flipper worker 滤波顺序相同）。
    }
    portEXIT_CRITICAL_ISR(&_rawMux);
}

bool RFSignalDecoder::available() {
#ifdef ARDUINO_ARCH_ESP32
    if (_ookRmtActive && readOokCapture()) return true;
#endif
    uint32_t pulses[MAX_PULSES];
    bool levels[MAX_PULSES];
    uint16_t count = 0;
    bool overrun = false;
    portENTER_CRITICAL(&_rawMux);
    if (!_rawMode) {
        // 最后一个已结束的段无需等到下一次完整边沿才交给协议机。
        // 当前反向电平已保持 >= 滤波门限，才能证明它不是待粘合的短毛刺。
        if (_rxPendingValid && (micros() - _lastPulseTime >= _minPulseWidth) &&
            (digitalRead(_gdo2Pin) == HIGH) != _rxPendingLevel) {
            rawPushIsr(_rxPendingLevel, _rxAccumUs);
            _rxPendingValid = false;
            _rxAccumUs = 0;
        }
        overrun = _rawOverrun;
        if (overrun) {
            // 丢段后不能拼接两端伪造完整帧；清队列，再等真实帧锚点。
            _rawTail = _rawHead;
            _rawOverrun = false;
            _rxPendingValid = false;
            _rxAccumUs = 0;
            _rxOverruns++;
        } else {
            while (count < MAX_PULSES && _rawTail != _rawHead) {
                pulses[count] = ((uint32_t)_rawRing[_rawTail].durationHigh << 16) |
                                _rawRing[_rawTail].durUs;
                levels[count] = _rawRing[_rawTail].level != 0;
                _rawTail = (uint16_t)((_rawTail + 1) % RAW_RING_SIZE);
                count++;
            }
        }
    }
    portEXIT_CRITICAL(&_rawMux);
    if (overrun) {
        RFProtocolRegistry::instance().resetAll();
        auto& registry = RFProtocolRegistry::instance();
        for (uint8_t d = 0; d < registry.count(); d++)
            _alternateStates[d] = registry.at(d)->saveState();
        LOG_W("[Decoder] RX queue overrun: discarded incomplete stream");
    }
    // 段数不是帧边界；协议机可以跨任意大小的消费批次继续收帧。
    return count != 0 && decodeSignal(pulses, levels, count);
}

bool RFSignalDecoder::available(RFSignal &signal) {
    if (!available()) return false;
    memcpy(&signal, &_decodedSignal, sizeof(RFSignal));
    return true;
}

RFSignal* RFSignalDecoder::getLastSignal() { return &_lastSignal; }

void RFSignalDecoder::reset() {
#ifdef ARDUINO_ARCH_ESP32
    _ookFrame.reset();
#endif
    clearBuffer();
    RFProtocolRegistry::instance().resetAll();  // 全部协议机统一复位
    _invert = false;
    auto& registry = RFProtocolRegistry::instance();
    for (uint8_t d = 0; d < registry.count(); d++)
        _alternateStates[d] = registry.at(d)->saveState();
    memset(&_decodedSignal, 0, sizeof(_decodedSignal));
    _rxSegments = _rxLowGuards = _rxHighGuards = 0;
    _lastFrameCandidateAt = _lastFrameProgressAt = 0;
    _traceHead = _traceCount = 0;
    memset(_protocolProgress, 0, sizeof(_protocolProgress));
}

void RFSignalDecoder::clearBuffer() {
    portENTER_CRITICAL(&_rawMux);
    _rawHead = _rawTail = 0;
    _rawOverrun = false;
    _rxStarted = false;
    _rxPendingValid = false;
    _rxAccumUs = 0;
    _rxGlitches = 0;
    _edgeTraceHead = _edgeTraceCount = 0;
    _edgeTraceTotal = 0;
    _packetGap = 15000;
    _lastPulseTime = micros();
    portEXIT_CRITICAL(&_rawMux);
}

// 连续段回放：仅真实 Guard 可以启动协议机；两种极性各自保留跨批次状态。
// 不注入虚拟帧头，也不把“尚未确认”交给直方图算法猜测后抢先上报。
bool RFSignalDecoder::decodeFlipperStyle(const uint16_t* pulses, const bool* levels,
                                         uint16_t count, RFSignal &signal) {
    return decodeProtocolStream(pulses, levels, count, signal);
}

bool RFSignalDecoder::decodeFlipperStyle(const uint32_t* pulses, const bool* levels,
                                        uint16_t count, RFSignal& signal) {
    return decodeProtocolStream(pulses, levels, count, signal);
}

template <typename Pulse>
bool RFSignalDecoder::decodeProtocolStream(const Pulse* pulses, const bool* levels,
                                          uint16_t count, RFSignal& signal) {
    if (count == 0 || !pulses || !levels) return false;
    auto& reg = RFProtocolRegistry::instance();
    const uint8_t nDec = reg.count();
    RFDecState primaryProgress[RFProtocolRegistry::MAX_DECODERS];
    bool found = false;
    bool primaryFound = false;
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        if (attempt == 1)
            for (uint8_t d = 0; d < nDec; d++) reg.at(d)->restoreState(_alternateStates[d]);
        bool inv = attempt == 0 ? _invert : !_invert;
        for (uint16_t i = 0; i < count; i++) {
            // 超过1秒才视为一次独立按键间的静默；50..1000ms仍可是真实Guard。
            if (pulses[i] > 1000000UL) {
                reg.resetAll();
                continue;
            }
            if (pulses[i] < _minPulseWidth) continue;
            bool lv = inv ? !levels[i] : levels[i];
            for (uint8_t d = 0; d < nDec; d++) {
                auto* dec = reg.at(d);
                uint8_t beforeStep = dec->state();
                bool guard = !lv && pulses[i] >= (dec->protoId() == 200 ? 600U : 2000U);
                RFDecState before;
                if (guard) before = dec->saveState();
                bool ready = dec->feed(lv, pulses[i]);
                auto& progress = _protocolProgress[d][inv ? 1 : 0];
                if (guard && beforeStep == 0 && dec->state() == 1) progress.anchors++;
                if (guard && beforeStep == 2 &&
                    (dec->protoId() != 200 || dec->saveState().cnt == 0)) {
                    progress.ends++;
                    progress.lastBits = before.cnt;
                    progress.lastData = before.data;
                    if (dec->protoId() == 200 && before.cnt >= 24 && before.cnt <= 64 &&
                        dec->saveState().lastBits == before.cnt && dec->state() == 1 &&
                        dec->saveState().cnt == 0) {
                        _lastFrameCandidateAt = millis();
                        logCompleteFrame(count - 1 - i, levels[i], before.cnt);
                    }
                }
                if (beforeStep != 0 && dec->state() == 0) progress.badPairs++;
                // 具体协议优先于 PWM24 兼容路径，两种极性命中都参与选择。
                if (!ready || (found && (signal.protocolId != 200 || dec->protoId() == 200)))
                    continue;
                found = true;
                primaryFound = attempt == 0;
                uint8_t bits = dec->frameBits();
                uint64_t data = dec->frameData();
                memset(signal.data, 0, sizeof(signal.data));
                for (uint8_t bit = 0; bit < bits; bit++)
                    if (data & (1ULL << (bits - 1 - bit)))
                        signal.data[bit / 8] |= (uint8_t)(1 << (7 - bit % 8));
                signal.bitCount = bits;
                signal.shortPulse = dec->frameTe();
                signal.longPulse = dec->frameLongPulse();
                signal.guardDurationUs = dec->frameGuard();
                signal.syncPulse = (uint16_t)min((uint32_t)65535, signal.guardDurationUs);
                signal.encodingType = ENCODING_PWM;
                signal.signalType = (bits == 24 || bits == 25) ? SIGNAL_LEARNING_CODE
                                    : (bits <= 18) ? SIGNAL_FIXED_CODE : SIGNAL_UNKNOWN;
                if (dec->protoId() == 200)
                    snprintf(signal.protocolName, sizeof(signal.protocolName), "PWM%u", bits);
                else
                    strncpy(signal.protocolName, dec->protoName(), sizeof(signal.protocolName) - 1);
                signal.protocolName[sizeof(signal.protocolName) - 1] = '\0';
                signal.protocolId = dec->protoId();
                signal.protocolValidated = true;
                signal.repeatCount = 2; // 协议机已确认两个等长同码完整帧
            }
        }
        // 即使命中也继续消费本批剩余段，下一批的状态必须与真实流位置一致。
        for (uint8_t d = 0; d < nDec; d++) {
            const RFDecState state = reg.at(d)->saveState();
            // 多个位对已通过真实帧头后的协议校验，可帮助扫频器保留弱信号。
            if (state.step && state.cnt >= 6) _lastFrameProgressAt = millis();
            if (attempt == 0) primaryProgress[d] = state;
            else _alternateStates[d] = state;
        }
    }
    if (found && !primaryFound) {
        _invert = !_invert;
        for (uint8_t d = 0; d < nDec; d++) _alternateStates[d] = primaryProgress[d];
        LOG_I("[Flipper] polarity learned: %s", _invert ? "INVERTED" : "NORMAL");
    } else {
        for (uint8_t d = 0; d < nDec; d++) reg.at(d)->restoreState(primaryProgress[d]);
    }
    return found;
}

bool RFSignalDecoder::decodeSignal(const uint32_t* pulses, const bool* levels,
                                    uint16_t count) {
    _totalSignals++;
    memset(&_decodedSignal, 0, sizeof(_decodedSignal));
    _decodedSignal.pulseCount = count;
    _decodedSignal.timestamp = millis();
    for (uint16_t i = 0; i < count && i < MAX_PULSES; i++) {
        _decodedSignal.pulses[i] = (uint16_t)min((uint32_t)65535, pulses[i]);
        _decodedSignal.pulseLevels[i] = levels[i];
        _rxSegments++;
        if (pulses[i] > 2000) {
            if (levels[i]) _rxHighGuards++; else _rxLowGuards++;
        }
        _traceDur[_traceHead] = pulses[i];
        _traceLevel[_traceHead] = levels[i];
        _traceHead = (_traceHead + 1) % RX_TRACE_SIZE;
        if (_traceCount < RX_TRACE_SIZE) _traceCount++;
        if (_verboseDebug)
            LOG_D("[Pulse] %c %uus", levels[i] ? 'H' : 'L', pulses[i]);
    }
    if (!decodeFlipperStyle(pulses, levels, count, _decodedSignal)) return false;
    pulsesToHex(_decodedSignal.data, _decodedSignal.bitCount, _decodedSignal.hexString);
    pulsesToBinary(_decodedSignal.data, _decodedSignal.bitCount, _decodedSignal.binaryString);
    memcpy(&_lastSignal, &_decodedSignal, sizeof(RFSignal));
    _decodedSignals++;
    return true;
}

void RFSignalDecoder::logReceiveDiagnostics(bool strongSignal) {
#if LOG_COMPILE_MAX >= 4
    if (Log::getLevel() < LogLevel::DEBUG) return;
#ifdef ARDUINO_ARCH_ESP32
    if (_ookRmtActive) _ookCapture.printDiagnostics();
#endif
#else
    return;
#endif
    // 不依赖-45dBm硬门限；此前OOK270弱信号窗口完全没有打印，无法核对滤波。
    if (_edgeTraceDumps < 2) {
        RawSeg edges[RX_TRACE_SIZE];
        uint32_t total, glitches;
        uint16_t count = edgeSnapshot(edges, RX_TRACE_SIZE, total, glitches);
        if (count >= 50 && total != _lastEdgeDumpTotal) {
            _edgeTraceDumps++;
            _lastEdgeDumpTotal = total;
            LOG_D("[RX.edges] snapshot=%u count=%u total=%lu filtered=%lu threshold=%uus (before filter)",
                  _edgeTraceDumps, count, (unsigned long)total, (unsigned long)glitches, _minPulseWidth);
            for (uint16_t offset = 0; offset < count; offset += 16) {
                char line[192] = {0}; size_t pos = 0;
                for (uint16_t k = offset; k < count && k < offset + 16; k++) {
                    uint32_t dur = ((uint32_t)edges[k].durationHigh << 16) | edges[k].durUs;
                    pos += snprintf(line + pos, sizeof(line) - pos, "%c%lu ",
                                    edges[k].level ? '+' : '-', (unsigned long)dur);
                }
                LOG_D("[RX.edges] %s", line);
            }
        }
    }
    uint32_t glitches;
    portENTER_CRITICAL(&_rawMux);
    glitches = _rxGlitches;
    portEXIT_CRITICAL(&_rawMux);
    LOG_D("[RX.decode] segments=%lu glitches=%lu filter=%uus guards(L/H)=%lu/%lu",
          (unsigned long)_rxSegments, (unsigned long)glitches, _minPulseWidth,
          (unsigned long)_rxLowGuards, (unsigned long)_rxHighGuards);
    auto& reg = RFProtocolRegistry::instance();
    for (uint8_t d = 0; d < reg.count(); d++) {
        const auto& n = _protocolProgress[d][0];
        const auto& i = _protocolProgress[d][1];
        if (!(n.anchors || i.anchors || n.ends || i.ends || n.badPairs || i.badPairs)) continue;
        LOG_D("[RX.proto] %s N/I anchors=%lu/%lu ends=%lu/%lu bits=%u/%u "
              "data=%08lX%08lX/%08lX%08lX bad=%lu/%lu",
              reg.at(d)->protoName(), (unsigned long)n.anchors, (unsigned long)i.anchors,
              (unsigned long)n.ends, (unsigned long)i.ends, n.lastBits, i.lastBits,
              (unsigned long)(n.lastData >> 32), (unsigned long)(uint32_t)n.lastData,
              (unsigned long)(i.lastData >> 32), (unsigned long)(uint32_t)i.lastData,
              (unsigned long)n.badPairs, (unsigned long)i.badPairs);
    }
    if (!strongSignal || _traceDumps >= 2 || _traceCount < 50 ||
        _lastTraceDumpSegmentCount == _rxSegments) return;
    _traceDumps++;
    _lastTraceDumpSegmentCount = _rxSegments;
    LOG_D("[RX.pulses] snapshot=%u count=%u (+HIGH/-LOW us, filtered stream)",
          _traceDumps, _traceCount);
    uint16_t first = (_traceHead + RX_TRACE_SIZE - _traceCount) % RX_TRACE_SIZE;
    for (uint16_t offset = 0; offset < _traceCount; offset += 16) {
        char line[192] = {0};
        size_t pos = 0;
        for (uint16_t k = offset; k < _traceCount && k < offset + 16; k++) {
            uint16_t index = (first + k) % RX_TRACE_SIZE;
            pos += snprintf(line + pos, sizeof(line) - pos, "%c%u ",
                            _traceLevel[index] ? '+' : '-', (unsigned)_traceDur[index]);
        }
        LOG_D("[RX.pulses] %s", line);
    }
}

void RFSignalDecoder::logCompleteFrame(uint16_t remainingSegments, bool guardLevel, uint8_t bits) {
#if LOG_COMPILE_MAX >= 4
    if (Log::getLevel() < LogLevel::DEBUG) return;
#else
    return;
#endif
    if (_frameTraceDumps >= 2 || remainingSegments >= _traceCount) return;
    uint16_t end = (_traceHead + RX_TRACE_SIZE - 1 - remainingSegments) % RX_TRACE_SIZE;
    uint16_t available = _traceCount - remainingSegments;
    // PWM完整候选固定由bits个位对和短同步HIGH组成，精确回溯首Guard。
    // 不能在短TE模式下把最近一个普通长LOW数据位当成帧头。
    uint16_t distance = 2 * bits + 2;
    if (distance < available) {
        uint16_t start = (end + RX_TRACE_SIZE - distance) % RX_TRACE_SIZE;
        if (_traceLevel[start] != guardLevel || _traceDur[start] < 600) return;
        _frameTraceDumps++;
        LOG_D("[RX.frame] capture=%u pairs=%u segments=%u guard=%luus (+HIGH/-LOW us)",
              _frameTraceDumps, bits, distance + 1, (unsigned long)_traceDur[end]);
        for (uint16_t offset = 0; offset <= distance; offset += 16) {
            char line[192] = {0};
            size_t pos = 0;
            for (uint16_t k = offset; k <= distance && k < offset + 16; k++) {
                uint16_t index = (start + k) % RX_TRACE_SIZE;
                pos += snprintf(line + pos, sizeof(line) - pos, "%c%lu ",
                                _traceLevel[index] ? '+' : '-', (unsigned long)_traceDur[index]);
            }
            LOG_D("[RX.frame] %s", line);
        }
        return;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// 直方图聚类时序分析（改进版 - 修复 #2）
//
// 原问题：
//   - 排除区固定 ±5 bucket(±250us)，短脉冲分布右尾会在排除区外形成"肩部"
//     被误识别为第二个峰，导致 long 被低估（如真实 900us 识别为 605us）
//   - 平滑窗口过小(3)，无法有效抑制肩部
//   - 单簇判定阈值过低(1/8)，噪声峰也被当作第二峰
//
// 改进：
//   1. 排除区按 peak1 位置的 60% 动态扩展（短脉冲越宽，右尾延伸越远）
//   2. 平滑窗口扩大到 5 bucket，更强抑制肩部
//   3. 单簇判定阈值提高到 1/4
//   4. peak2 必须在 peak1 的 1.4~5 倍范围内才视为有效长脉冲峰
// ──────────────────────────────────────────────────────────────────────────────
void RFSignalDecoder::analyzeTiming(const uint16_t* pulses, uint16_t count,
                                     uint16_t &shortPulse, uint16_t &longPulse,
                                     uint16_t &syncPulse) {
    shortPulse = longPulse = syncPulse = 0;
    if (count < 4) return;

    // ── 第一步：快速找最长脉冲，确定同步门限 ──────────────────────────────
    uint32_t maxWidth = 0;
    for (uint16_t i = 0; i < count; i++)
        if (pulses[i] > maxWidth) maxWidth = pulses[i];

    // 同步头门限：最大脉冲的 40%；或至少 5ms（常见同步头下限）
    uint32_t syncThresh = maxWidth * 4 / 10;
    if (syncThresh < 5000) syncThresh = 5000;

    // 记录最长的"同步候选"
    for (uint16_t i = 0; i < count; i++) {
        if (pulses[i] >= syncThresh && pulses[i] > syncPulse)
            syncPulse = pulses[i];
    }

    // ── 第二步：只对"数据脉冲"建直方图 ────────────────────────────────────
    // NOTE: 直方图用函数静态存储（非栈上）：analyzeTiming 仅在接收任务上下文
    // 被 decodeSignal 调用，ISR 从不进入，12KB任务栈下可省约1.6KB栈峰值。
    const uint16_t BUCKET = 50;           // 50us 分辨率
    const uint16_t HSIZE  = 400;          // 覆盖 0~20ms
    static uint16_t hist[400];
    memset(hist, 0, sizeof(hist));

    uint16_t validCount = 0;
    for (uint16_t i = 0; i < count; i++) {
        uint16_t w = pulses[i];
        if (w < _minPulseWidth || w >= syncThresh) continue;
        uint16_t bucket = w / BUCKET;
        if (bucket >= HSIZE) bucket = HSIZE - 1;
        hist[bucket]++;
        validCount++;
    }

    if (validCount < 4) return;

    // 平滑（5-bucket 滑动窗口，更强抑制肩部干扰；静态存储省栈，见上NOTE）
    static uint16_t smoothed[400];
    for (uint16_t i = 0; i < HSIZE; i++) {
        uint32_t s = hist[i];
        if (i > 0)       s += hist[i - 1];
        if (i > 1)       s += hist[i - 2];
        if (i < HSIZE-1) s += hist[i + 1];
        if (i < HSIZE-2) s += hist[i + 2];
        smoothed[i] = (uint16_t)(s / 5);
    }

    // ── 第三步：找两个最高峰 ───────────────────────────────────────────────
    uint16_t peak1 = 0, peak1Cnt = 0;
    for (uint16_t i = 0; i < HSIZE; i++) {
        if (smoothed[i] > peak1Cnt) { peak1Cnt = smoothed[i]; peak1 = i; }
    }
    if (peak1Cnt == 0) return;

    // 动态排除区：peak1 位置的 60%（至少 5 bucket=250us）
    // 短脉冲越宽（如 300us=6 bucket），右尾延伸越远，需要更大排除区
    // 原固定 ±5 bucket 在短脉冲较宽时不足以屏蔽肩部
    int32_t excludeRange = (int32_t)peak1 * 6 / 10;
    if (excludeRange < 5) excludeRange = 5;

    uint16_t peak2 = 0, peak2Cnt = 0;
    for (uint16_t i = 0; i < HSIZE; i++) {
        if (abs((int32_t)i - (int32_t)peak1) <= excludeRange) continue;
        if (smoothed[i] > peak2Cnt) { peak2Cnt = smoothed[i]; peak2 = i; }
    }

    // 只有一个峰时：所有脉冲等宽（如纯 PPM），不强行拆两簇
    // 阈值从 1/8 提高到 1/4，避免噪声肩部被当作第二峰
    if (peak2Cnt < peak1Cnt / 4) {
        // 单簇信号：short = long = 峰中心
        uint32_t sum = 0, cnt2 = 0;
        for (uint16_t i = 0; i < HSIZE; i++) {
            if (smoothed[i] > 0) { sum += (uint32_t)(i * BUCKET + BUCKET/2) * hist[i]; cnt2 += hist[i]; }
        }
        shortPulse = longPulse = (uint16_t)(cnt2 > 0 ? sum / cnt2 : peak1 * BUCKET + BUCKET/2);
        return;
    }

    // 确保 peak1 < peak2（短脉冲 < 长脉冲）
    if (peak1 > peak2) { uint16_t t = peak1; peak1 = peak2; peak2 = t;
                         uint16_t tc = peak1Cnt; peak1Cnt = peak2Cnt; peak2Cnt = tc; }

    // 峰值合理性校验：peak2 必须在 peak1 的 1.4~5 倍范围内
    // PWM 协议的 long/short 比例通常在 2~4 之间（PT2262=3, EV1527=3, Manchester=2）
    // 超出范围说明 peak2 是噪声或同步头残余，不可信
    if (peak2 < peak1 * 14 / 10 || peak2 > peak1 * 5) {
        // 视为单簇，回退到加权平均
        uint32_t sum = 0, cnt2 = 0;
        for (uint16_t i = 0; i < HSIZE; i++) {
            if (hist[i] == 0) continue;
            sum += (uint32_t)(i * BUCKET + BUCKET/2) * hist[i];
            cnt2 += hist[i];
        }
        shortPulse = longPulse = (uint16_t)(cnt2 > 0 ? sum / cnt2 : peak1 * BUCKET + BUCKET/2);
        return;
    }

    // ── 第四步：加权平均求精确中心 ─────────────────────────────────────────
    uint16_t mid = (peak1 + peak2) / 2;   // 分割边界

    uint32_t sumS = 0, cntS = 0, sumL = 0, cntL = 0;
    for (uint16_t i = 0; i < HSIZE; i++) {
        if (hist[i] == 0) continue;
        uint16_t center = (uint16_t)(i * BUCKET + BUCKET / 2);
        if (i <= mid) { sumS += (uint32_t)center * hist[i]; cntS += hist[i]; }
        else          { sumL += (uint32_t)center * hist[i]; cntL += hist[i]; }
    }

    shortPulse = (uint16_t)(cntS > 0 ? sumS / cntS : peak1 * BUCKET + BUCKET/2);
    longPulse  = (uint16_t)(cntL > 0 ? sumL / cntL : peak2 * BUCKET + BUCKET/2);
}

// ──────────────────────────────────────────────────────────────────────────────
// PWM 解码（改进版）
//
// 改进点：
//   1. 同时尝试两种相位（偶数起始 / 奇数起始），选位数更多的结果
//   2. 对同步头位置的检测不再依赖奇偶判断，直接找超阈值脉冲
//   3. 错误容忍改为：连续 3 个无效位才终止（而非5）
// ──────────────────────────────────────────────────────────────────────────────
bool RFSignalDecoder::decodePWM(const uint16_t* pulses, uint16_t count,
                                  RFSignal &signal) {
    if (count < 16) return false;

    uint16_t shortPulse = signal.shortPulse;
    uint16_t longPulse  = signal.longPulse;
    if (shortPulse == 0 || longPulse == 0) return false;

    float ratio = (float)longPulse / (float)shortPulse;
    if (ratio < 1.4f || ratio > 8.0f) return false;

    // 同步头阈值：长脉冲的 2.5x，但至少短脉冲的 6x
    uint32_t syncThresh = (uint32_t)longPulse * 5 / 2;
    if (syncThresh < (uint32_t)shortPulse * 6)
        syncThresh = (uint32_t)shortPulse * 6;

    // 收集同步头位置
    uint16_t syncPos[16];
    uint8_t  syncCnt = 0;
    for (uint16_t i = 0; i < count && syncCnt < 16; i++) {
        if (pulses[i] > syncThresh) syncPos[syncCnt++] = i;
    }

    if (_verboseDebug)
        LOG_D("[PWM] cnt=%d short=%d long=%d syncThr=%lu syncCnt=%d",
              count, shortPulse, longPulse, (unsigned long)syncThresh, syncCnt);

    uint8_t  bestData[MAX_BITS / 8] = {0};
    uint16_t bestBits  = 0;
    int32_t  bestScore = -10000;   // 评分 = validBits - 2×errorBits
    uint16_t bestStart = 0;

    // 对每个同步头后的区间，尝试 phase0 和 phase1 两种起始偏移
    auto tryDecode = [&](uint16_t start, uint16_t end) {
        for (uint8_t phase = 0; phase <= 1; phase++) {
            uint8_t  data[MAX_BITS / 8] = {0};
            uint16_t errCount = 0;
            uint16_t bits = decodePWMFrameRaw(pulses, start + phase, end,
                                              shortPulse, longPulse, data, &errCount);
            if (bits < 12) continue;

            // 评分：有效位为主，错误位双倍扣分
            // 避免在错误相位下"凑出"较多位数（曾导致 38bit 错误结果）
            int32_t score = (int32_t)bits - (int32_t)errCount * 2;
            if (score > bestScore) {
                bestScore = score;
                bestBits  = bits;
                bestStart = start + phase;
                memcpy(bestData, data, (bits + 7) / 8);
            }
        }
    };

    if (syncCnt == 0) {
        // 无同步头：从头部直接尝试
        tryDecode(0, count);
    } else {
        // ISR 缓冲通常是 [完整数据 + 帧尾 Guard]；第一个 Guard 之前也有完整帧。
        tryDecode(0, syncPos[0]);
        for (uint8_t s = 0; s < syncCnt; s++) {
            uint16_t frameStart = syncPos[s] + 1;
            uint16_t frameEnd   = (s + 1 < syncCnt) ? syncPos[s + 1] : count;
            if (frameEnd <= frameStart) continue;
            tryDecode(frameStart, frameEnd);
        }
    }
    // 注：原"盲试 offset 0~3"的兜底逻辑已删除
    // 该逻辑会在多帧拼接或相位错乱时从任意起点凑出错误位数（如 38bit）
    // 正确做法是依赖同步头对齐 + phase 0/1，不再盲目尝试任意起点

    if (bestBits >= 12) {
        signal.encodingType = ENCODING_PWM;
        signal.bitCount     = bestBits;
        memcpy(signal.data, bestData, (bestBits + 7) / 8);
        LOG_D("[PWM] OK: %d bits from idx %d (score=%ld)",
              bestBits, bestStart, (long)bestScore);
        return true;
    }
    return false;
}

// 解码单帧原始 PWM 数据，返回位数
// errOut：输出错误配对数，供调用方做质量评分
uint16_t RFSignalDecoder::decodePWMFrameRaw(const uint16_t* pulses, uint16_t start,
                                              uint16_t end, uint16_t shortPulse,
                                              uint16_t longPulse, uint8_t* data,
                                              uint16_t* errOut) {
    if (shortPulse == 0 || longPulse == 0 || start >= end) {
        if (errOut) *errOut = 0;
        return 0;
    }

    uint16_t bitIndex  = 0;
    uint16_t errCount  = 0;

    // 容忍区间：默认 ±40%，但必须保证 short 和 long 区间不重叠
    // 重叠区会让中间宽度的脉冲既算 short 又算 long，产生错误位
    int32_t tolS = (int32_t)shortPulse * 4 / 10;
    int32_t tolL = (int32_t)longPulse  * 4 / 10;
    if (tolS < 80)  tolS = 80;
    if (tolL < 120) tolL = 120;

    // 防止 short/long 区间重叠：若重叠则各自向中点收紧
    // 例：short=302,tolS=120 → [182,422]；long=605,tolL=242 → [363,847]
    //     重叠 60us，各让 30us → tolS=90,tolL=212，区间不再相交
    int32_t gap = (int32_t)longPulse - (int32_t)shortPulse;
    if (gap > 0) {
        int32_t overlap = tolS + tolL - gap;
        if (overlap > 0) {
            int32_t shrink = overlap / 2 + 1;
            tolS = (tolS - shrink > 50) ? (tolS - shrink) : 50;
            tolL = (tolL - shrink > 50) ? (tolL - shrink) : 50;
        }
    }

    uint16_t tolSu = (uint16_t)tolS;
    uint16_t tolLu = (uint16_t)tolL;

    // 停止脉冲：超过 longPulse 的 2x 视为同步/间隔
    uint16_t stopThresh = longPulse * 2;

    for (uint16_t i = start; i + 1 < end && bitIndex < MAX_BITS; i += 2) {
        uint16_t p0 = pulses[i];
        uint16_t p1 = pulses[i + 1];

        // 遇到长间隔则停止
        if (p0 > stopThresh || p1 > stopThresh) break;

        bool p0Short = (p0 >= shortPulse - tolSu && p0 <= shortPulse + tolSu);
        bool p0Long  = (p0 >= longPulse  - tolLu && p0 <= longPulse  + tolLu);
        bool p1Short = (p1 >= shortPulse - tolSu && p1 <= shortPulse + tolSu);
        bool p1Long  = (p1 >= longPulse  - tolLu && p1 <= longPulse  + tolLu);

        if (p0Long && p1Short) {
            // 逻辑 1：长高 + 短低
            data[bitIndex / 8] |= (1 << (7 - (bitIndex % 8)));
            bitIndex++;
            errCount = 0;
        } else if (p0Short && p1Long) {
            // 逻辑 0：短高 + 长低
            data[bitIndex / 8] &= ~(1 << (7 - (bitIndex % 8)));
            bitIndex++;
            errCount = 0;
        } else {
            // 无效位：跳过但计入错误
            errCount++;
            if (errCount >= 3) break;
        }
    }

    if (errOut) *errOut = errCount;
    return bitIndex;
}

// ──────────────────────────────────────────────────────────────────────────────
// PPM 解码（改进版）
// ──────────────────────────────────────────────────────────────────────────────
bool RFSignalDecoder::decodePPM(const uint16_t* pulses, uint16_t count,
                                  RFSignal &signal) {
    if (count < 20) return false;

    uint16_t shortPulse = signal.shortPulse;
    uint16_t longPulse  = signal.longPulse;
    if (shortPulse == 0 || longPulse == 0) return false;

    uint16_t tol = (longPulse - shortPulse) / 3;
    if (tol < 80) tol = 80;

    uint8_t  data[MAX_BITS / 8] = {0};
    uint16_t bitIndex   = 0;
    uint16_t validCount = 0;

    for (uint16_t i = 0; i < count && bitIndex < MAX_BITS; i++) {
        uint16_t w = pulses[i];
        if (w > longPulse * 3) continue;   // 跳过超长同步头

        bool isShort = (w >= shortPulse - tol && w <= shortPulse + tol);
        bool isLong  = (w >= longPulse  - tol && w <= longPulse  + tol);

        if (isShort) {
            data[bitIndex / 8] &= ~(1 << (7 - (bitIndex % 8)));
            bitIndex++;
            validCount++;
        } else if (isLong) {
            data[bitIndex / 8] |= (1 << (7 - (bitIndex % 8)));
            bitIndex++;
            validCount++;
        }
    }

    if (bitIndex >= 16 && validCount * 2 >= count / 2) {
        signal.encodingType = ENCODING_PPM;
        signal.bitCount     = bitIndex;
        memcpy(signal.data, data, (bitIndex + 7) / 8);
        return true;
    }
    return false;
}

// ──────────────────────────────────────────────────────────────────────────────
// 曼彻斯特解码（v3：同步头检测 + 跳变沿采样）
//
// 实测信号特征（418MHz 遥控器）：
//   short≈300us(T), long≈600us(2T), ratio≈2.0
//   帧以连续两个 2T 脉冲开头（如 H564 L586），即同步头
//   同步头后跟随 Manchester 编码数据
//
// Manchester 规则（IEEE 802.3）：
//   位周期 = 2T，每位中间必有跳变
//   低→高（上升沿）= 0
//   高→低（下降沿）= 1
//
// 同步头检测：
//   连续两个 2T 脉冲（共 4T）违反 Manchester 规则，必为同步标记
//   同步头之后第一个 T 脉冲的边沿是第一个数据位的中间跳变
//
// 解码策略：
//   1. 扫描 buffer 找连续两个 2T 作为同步头
//   2. 从同步头后开始，用边沿方向解码每位
//   3. 一个位周期内的脉冲组合：T+T 或 2T
// ──────────────────────────────────────────────────────────────────────────────
bool RFSignalDecoder::decodeManchester(const uint16_t* pulses, const bool* levels,
                                        uint16_t count, RFSignal &signal) {
    if (count < 20) return false;

    uint16_t T  = signal.shortPulse;
    uint16_t T2 = signal.longPulse;
    if (T == 0 || T2 == 0) return false;

    uint16_t tol = T / 4;
    if (tol < 60) tol = 60;

    // 尝试两种极性（同步头可能是 HHLL 或 LLHH 模式）
    for (uint8_t polarity = 0; polarity < 2; polarity++) {
        uint8_t  data[MAX_BITS / 8] = {0};
        uint16_t bitIndex = 0;

        // 步骤1：找同步头（连续两个 2T 脉冲）
        uint16_t syncIdx = 0;
        bool     foundSync = false;
        for (uint16_t i = 0; i + 1 < count; i++) {
            bool is2T_1 = (pulses[i]   >= T2 - tol && pulses[i]   <= T2 + tol);
            bool is2T_2 = (pulses[i+1] >= T2 - tol && pulses[i+1] <= T2 + tol);
            if (is2T_1 && is2T_2) {
                syncIdx = i + 2;   // 同步头后第一个脉冲
                foundSync = true;
                break;
            }
        }

        uint16_t startIdx = foundSync ? syncIdx : 1;
        if (startIdx >= count) continue;

        // 步骤2：从同步头后解码 Manchester 数据
        // 用"半位累加"策略：每个 T 脉冲推进半个位周期，每个 2T 脉冲推进一个位周期
        // 在位周期中点（半位）采样边沿方向决定位值
        bool    lastLevel = levels[startIdx - 1];
        bool    expectHalf = true;   // true=期待半位(T)，false=期待全位剩余(T)
        uint16_t errs = 0;

        for (uint16_t i = startIdx; i < count && bitIndex < MAX_BITS; i++) {
            uint16_t w = pulses[i];
            bool     lv = levels[i];

            bool isT  = (w >= T  - tol && w <= T  + tol);
            bool is2T = (w >= T2 - tol && w <= T2 + tol);

            if (!isT && !is2T) {
                errs++;
                if (errs > 4) break;
                lastLevel = lv;
                continue;
            }
            errs = 0;

            if (expectHalf) {
                // 期待半位（位周期前半）
                if (isT) {
                    // T 脉冲：这是前半位，记录边沿方向，等待后半位
                    expectHalf = false;
                    // 不产生位值，等待第二个 T 或确认 2T
                } else {
                    // 2T 脉冲：跨越整个位周期，位中间无跳变
                    // 这违反标准 Manchester，但可能是同步头后第一位
                    // 用 lastLevel 推断（保持上一位的值）
                    bool bit = (lastLevel == HIGH);
                    if (bit)
                        data[bitIndex / 8] |= (1 << (7 - (bitIndex % 8)));
                    bitIndex++;
                    // expectHalf 保持 true（2T = 完整位周期）
                }
            } else {
                // 已有前半位，期待后半位
                if (isT) {
                    // T+T：位中间有跳变（第一个 T 结束时），标准 Manchester
                    // 位值由边沿方向决定：上升沿=0，下降沿=1
                    // lastLevel 是前半位的电平，lv 是后半位的电平
                    // 第一个 T 结束时发生跳变：lastLevel → lv
                    bool bit = (lastLevel == HIGH && lv == LOW);  // 下降沿=1
                    if (bit)
                        data[bitIndex / 8] |= (1 << (7 - (bitIndex % 8)));
                    bitIndex++;
                    expectHalf = true;
                } else {
                    // 已有前半 T，又来 2T：异常，重置
                    expectHalf = true;
                }
            }
            lastLevel = lv;
        }

        if (polarity == 0) {
            // 第一次尝试的结果暂存，后面可能被第二次覆盖
            if (bitIndex >= 12) {
                signal.encodingType = ENCODING_MANCHESTER;
                signal.bitCount     = bitIndex;
                memcpy(signal.data, data, (bitIndex + 7) / 8);
                LOG_D("[MCH] OK: %d bits (sync=%d)", bitIndex, foundSync ? 1 : 0);
                return true;
            }
        } else {
            // 第二次尝试：只有位数更多才覆盖
            if (bitIndex > signal.bitCount && bitIndex >= 12) {
                signal.encodingType = ENCODING_MANCHESTER;
                signal.bitCount     = bitIndex;
                memcpy(signal.data, data, (bitIndex + 7) / 8);
                LOG_D("[MCH] OK: %d bits (sync=%d, inv)", bitIndex, foundSync ? 1 : 0);
                return true;
            }
        }
    }

    return false;
}

// ──────────────────────────────────────────────────────────────────────────────
// 差分曼彻斯特（BiPhase）解码
//
// 规则：
//   位周期开始处有跳变 = 逻辑 0
//   位周期开始处无跳变 = 逻辑 1
//   每位中间必有一次跳变
// ──────────────────────────────────────────────────────────────────────────────
bool RFSignalDecoder::decodeBiPhase(const uint16_t* pulses, const bool* levels,
                                      uint16_t count, RFSignal &signal) {
    if (count < 20) return false;

    uint16_t T   = signal.shortPulse;
    if (T == 0) return false;
    uint16_t tol = T / 2;
    if (tol < 80) tol = 80;

    uint8_t  data[MAX_BITS / 8] = {0};
    uint16_t bitIndex = 0;
    bool     expectTransition = true;  // 下一个半位是否开始新位

    for (uint16_t i = 0; i < count - 1 && bitIndex < MAX_BITS; ) {
        uint16_t w = pulses[i];
        bool     half1 = (w >= T - tol && w <= T + tol);
        bool     full1 = (w >= T * 2 - tol && w <= T * 2 + tol);

        if (full1) {
            // 全位 = 逻辑 1（无开始跳变，只有中间跳变合并为全位）
            data[bitIndex / 8] |= (1 << (7 - (bitIndex % 8)));
            bitIndex++;
            i++;
            expectTransition = true;
        } else if (half1 && i + 1 < count) {
            uint16_t w2 = pulses[i + 1];
            bool half2 = (w2 >= T - tol && w2 <= T + tol);
            if (half2) {
                // 两个半位 = 逻辑 0（有开始跳变）
                data[bitIndex / 8] &= ~(1 << (7 - (bitIndex % 8)));
                bitIndex++;
                i += 2;
                expectTransition = true;
            } else {
                i++;
            }
        } else {
            i++;
        }
    }

    if (bitIndex >= 16) {
        signal.encodingType = ENCODING_BI_PHASE;
        signal.bitCount     = bitIndex;
        memcpy(signal.data, data, (bitIndex + 7) / 8);
        return true;
    }
    return false;
}

// ──────────────────────────────────────────────────────────────────────────────
// 协议识别（根据时序参数匹配协议库）
// ──────────────────────────────────────────────────────────────────────────────
bool RFSignalDecoder::identifyProtocol(RFSignal &signal) {
    const ProtocolProfile* bestMatch = nullptr;
    int bestScore = 0;

    for (uint8_t i = 0; i < PROTOCOL_COUNT; i++) {
        const ProtocolProfile& p = PROTOCOLS[i];
        if (p.id == 255) continue;  // 跳过 RAW 兜底
        if (signal.bitCount != p.bitCount || signal.encodingType != p.encoding)
            continue;
        if (p.syncMin > 0 && (signal.syncPulse < p.syncMin || signal.syncPulse > p.syncMax))
            continue;

        int score = 0;

        // 短脉冲匹配
        if (signal.shortPulse >= p.shortMin && signal.shortPulse <= p.shortMax)
            score += 3;
        else
            continue;  // 短脉冲必须匹配

        // 长脉冲匹配
        if (signal.longPulse >= p.longMin && signal.longPulse <= p.longMax)
            score += 3;
        else
            continue;  // 长脉冲必须匹配

        // 同步脉冲匹配（可选）
        if (p.syncMin > 0 && p.syncMax > 0) {
            if (signal.syncPulse >= p.syncMin && signal.syncPulse <= p.syncMax)
                score += 4;
        } else {
            score += 1;  // 协议不强制同步头，加少量分
        }

        // 位数匹配
        if (p.bitCount > 0 && signal.bitCount > 0) {
            if (signal.bitCount == p.bitCount)
                score += 5;
            else if (abs((int)signal.bitCount - (int)p.bitCount) <= 2)
                score += 2;
        }

        // 编码方式匹配
        if (signal.encodingType == p.encoding || signal.encodingType == ENCODING_UNKNOWN)
            score += 2;

        if (score > bestScore) {
            bestScore = score;
            bestMatch = &p;
        }
    }

    if (bestMatch && bestScore >= 6) {
        strncpy(signal.protocolName, bestMatch->name, sizeof(signal.protocolName) - 1);
        signal.protocolId   = bestMatch->id;
        signal.encodingType = bestMatch->encoding;

        // 信号类型分类
        if (signal.bitCount >= 64)       signal.signalType = SIGNAL_ROLLING_CODE;
        else if (signal.bitCount == 24)  signal.signalType = SIGNAL_LEARNING_CODE;
        else if (signal.bitCount <= 18)  signal.signalType = SIGNAL_FIXED_CODE;
        else                             signal.signalType = SIGNAL_UNKNOWN;
        return true;
    }

    // 无匹配：使用通用描述
    if (signal.encodingType == ENCODING_PWM)
        snprintf(signal.protocolName, sizeof(signal.protocolName),
                 "OOK/%dbit", signal.bitCount);
    else if (signal.encodingType == ENCODING_MANCHESTER)
        snprintf(signal.protocolName, sizeof(signal.protocolName),
                 "MCH/%dbit", signal.bitCount);
    else
        strcpy(signal.protocolName, "UNKNOWN");

    signal.protocolId = 0;
    signal.signalType = SIGNAL_UNKNOWN;
    return false;
}

// ──────────────────────────────────────────────────────────────────────────────
// 工具函数
// ──────────────────────────────────────────────────────────────────────────────
bool RFSignalDecoder::decodePWMFrame(const uint16_t* pulses, uint16_t start,
                                      uint16_t end, uint16_t midPoint,
                                      RFSignal &signal) {
    uint8_t  data[MAX_BITS / 8] = {0};
    uint16_t shortP = midPoint / 2;
    uint16_t longP  = midPoint * 3 / 2;
    uint16_t bits   = decodePWMFrameRaw(pulses, start, end, shortP, longP, data);
    if (bits >= 16) {
        signal.encodingType = ENCODING_PWM;
        signal.bitCount     = bits;
        memcpy(signal.data, data, (bits + 7) / 8);
        return true;
    }
    return false;
}

bool RFSignalDecoder::extractBits(const uint16_t* pulses, uint16_t count,
                                   uint16_t shortPulse, uint16_t longPulse,
                                   uint8_t* data, uint16_t maxBits) {
    return decodePWMFrameRaw(pulses, 0, count, shortPulse, longPulse, data) > 0;
}

void RFSignalDecoder::pulsesToHex(const uint8_t* data, uint16_t bits, char* hexStr) {
    const char hex[] = "0123456789ABCDEF";
    uint16_t bytes = (bits + 7) / 8;
    for (uint16_t i = 0; i < bytes; i++) {
        hexStr[i * 2]     = hex[(data[i] >> 4) & 0x0F];
        hexStr[i * 2 + 1] = hex[data[i] & 0x0F];
    }
    hexStr[bytes * 2] = '\0';
}

void RFSignalDecoder::pulsesToBinary(const uint8_t* data, uint16_t bits, char* binStr) {
    for (uint16_t i = 0; i < bits; i++)
        binStr[i] = (data[i / 8] & (1 << (7 - (i % 8)))) ? '1' : '0';
    binStr[bits] = '\0';
}

uint8_t RFSignalDecoder::reverseBits(uint8_t byte) {
    byte = ((byte & 0xF0) >> 4) | ((byte & 0x0F) << 4);
    byte = ((byte & 0xCC) >> 2) | ((byte & 0x33) << 2);
    byte = ((byte & 0xAA) >> 1) | ((byte & 0x55) << 1);
    return byte;
}

uint32_t RFSignalDecoder::crc32(const uint8_t* data, uint16_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

bool RFSignalDecoder::isDuplicate(const RFSignal &s1, const RFSignal &s2) {
    if (s1.bitCount != s2.bitCount) return false;
    if (s1.encodingType != s2.encodingType) return false;
    uint16_t bytes = (s1.bitCount + 7) / 8;
    for (uint16_t i = 0; i < bytes; i++)
        if (s1.data[i] != s2.data[i]) return false;
    return true;
}

float RFSignalDecoder::calculateSimilarity(const RFSignal &s1, const RFSignal &s2) {
    if (s1.bitCount != s2.bitCount) return 0.0f;
    uint16_t match = 0;
    for (uint16_t i = 0; i < s1.bitCount; i++) {
        bool b1 = s1.data[i / 8] & (1 << (7 - (i % 8)));
        bool b2 = s2.data[i / 8] & (1 << (7 - (i % 8)));
        if (b1 == b2) match++;
    }
    return (float)match / s1.bitCount;
}

void RFSignalDecoder::printSignal(const RFSignal &signal) {
    LOG_I("\n╔══════════════════════════════════════════════════╗");
    LOG_I("║           RF SIGNAL DECODED                      ║");
    LOG_I("╠══════════════════════════════════════════════════╣");
    LOG_I("║ Protocol:  %-24s (ID:%3d) ║",
          signal.protocolName, signal.protocolId);
    LOG_I("║ Encoding:  %-15s                     ║",
          getEncodingName(signal.encodingType));
    LOG_I("║ Type:      %-15s                     ║",
          getSignalTypeName(signal.signalType));
    LOG_I("║ Freq:      %.2f MHz                          ║",
          signal.frequency);
    LOG_I("║ Bits:      %-5d   Pulses: %-5d             ║",
          signal.bitCount, signal.pulseCount);
    LOG_I("╠══════════════════════════════════════════════════╣");
    LOG_I("║ Short: %4dus  Long: %4dus  Sync: %5dus  ║",
          signal.shortPulse, signal.longPulse, signal.syncPulse);
    LOG_I("╠══════════════════════════════════════════════════╣");
    LOG_I("║ HEX: %-44s ║", signal.hexString);
    // 只打印前 40 bit 的二进制，避免输出过长
    char bin40[41] = {0};
    uint16_t showBits = signal.bitCount < 40 ? signal.bitCount : 40;
    memcpy(bin40, signal.binaryString, showBits);
    bin40[showBits] = '\0';
    LOG_I("║ BIN: %-44s%s ║",
          bin40, signal.bitCount > 40 ? "..." : "   ");
    LOG_I("╚══════════════════════════════════════════════════╝");
}

void RFSignalDecoder::printRawPulses(const RFSignal &signal) {
    LOG_D("\n--- Raw Pulses ---");
    for (uint16_t i = 0; i < signal.pulseCount && i < 60; i++)
        LOG_D("[%3d] %c %5d us", i,
              signal.pulseLevels[i] ? 'H' : 'L', signal.pulses[i]);
    if (signal.pulseCount > 60)
        LOG_D("... (%d more)", signal.pulseCount - 60);
    LOG_D("------------------");
}

void RFSignalDecoder::printStatistics() {
    LOG_I("\n--- Decoder Statistics ---");
    LOG_I("Total:   %lu", _totalSignals);
    LOG_I("Decoded: %lu", _decodedSignals);
    LOG_I("Failed:  %lu", _failedDecodes);
    if (_totalSignals > 0)
        LOG_I("Rate:    %.1f%%",
              100.0f * _decodedSignals / _totalSignals);
    LOG_I("--------------------------");
}

const char* RFSignalDecoder::getEncodingName(EncodingType enc) {
    switch (enc) {
        case ENCODING_PWM:       return "PWM/OOK";
        case ENCODING_PPM:       return "PPM";
        case ENCODING_MANCHESTER:return "Manchester";
        case ENCODING_BI_PHASE:  return "Bi-Phase";
        case ENCODING_RAW:       return "RAW";
        default:                 return "Unknown";
    }
}

const char* RFSignalDecoder::getSignalTypeName(SignalType type) {
    switch (type) {
        case SIGNAL_FIXED_CODE:   return "Fixed Code";
        case SIGNAL_ROLLING_CODE: return "Rolling Code";
        case SIGNAL_LEARNING_CODE:return "Learning Code";
        case SIGNAL_RAW:          return "Raw";
        default:                  return "Unknown";
    }
}
