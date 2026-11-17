#ifndef RF_PROTOCOL_PWM24_H
#define RF_PROTOCOL_PWM24_H

#include "RFProtocolRegistry.h"

// 高/低脉宽编码兼容路径。保留实际完整帧长度(24..64位)，不推测厂商。
// 必须有真实Guard、全部位对有效、短同步高电平及两个同码同长度完整帧。
class FramedPWMDecoder : public IRFProtocolDecoder {
public:
    void reset() override {
        _s = {};
        _out = 0; _outBits = 0; _short = _long = 0; _guard = 0; _ready = false;
    }
    bool feed(bool level, uint32_t dur) override {
        _ready = false;
        // 短TE遥控器的31T同步可小于2ms。已成帧后依据实测TE判断，
        // 不能把普通900us数据LOW误当同步；空闲时只作为候选锚点。
        bool guard = !level && (dur >= 2000 || (_s.step == 0 && dur >= 600));
        if (!level && dur >= 600 && _s.step == 2 && _s.cnt >= 24)
            guard = guard || (dur >= (_s.longSum / _s.cnt) * 5 &&
                              dur >= (_s.shortSum / _s.cnt) * 12);
        if (guard) {
            if (_s.step == 2 && _s.cnt >= 24 && _s.cnt <= 64) {
                uint16_t sp = _s.shortSum / _s.cnt;
                uint16_t lp = _s.longSum / _s.cnt;
                bool complete = near(_s.teLast, sp) && dur >= (uint32_t)lp * 5 &&
                                dur >= (uint32_t)sp * 12 &&
                                near(_s.shortMin, sp) && near(_s.shortMax, sp) &&
                                near(_s.longMin, lp) && near(_s.longMax, lp);
                if (complete) {
                    if (_s.lastBits == _s.cnt && _s.last == _s.data &&
                        near(sp, _s.previousShort) && near(lp, _s.previousLong)) {
                        _out = _s.data; _outBits = _s.cnt; _short = sp; _long = lp; _guard = dur;
                        _ready = true;
                    }
                    _s.last = _s.data; _s.lastBits = _s.cnt;
                    _s.previousShort = sp; _s.previousLong = lp;
                } else {
                    _s.lastBits = 0;
                }
            } else {
                _s.lastBits = 0;
            }
            _s.step = 1; _s.cnt = 0; _s.data = 0;
            _s.shortSum = _s.longSum = 0;
            _s.shortMin = _s.shortMax = _s.longMin = _s.longMax = 0;
            return _ready;
        }
        if (_s.step == 1 && level && dur >= 30 && dur <= 1800) {
            _s.teLast = dur; _s.step = 2;
        } else if (_s.step == 2 && !level && dur >= 30 && dur <= 1800) {
            uint32_t sp = min(_s.teLast, dur), lp = max(_s.teLast, dur);
            // 实机2:1波形的边沿抖动可把单个位对拉到约1.6:1；
            // 分簇一致性仍由逐帧均值约束，不能用1.7硬门限丢掉整帧。
            bool pair = lp * 10 >= sp * 14 && lp * 10 <= sp * 45 + 400;
            if (!pair || _s.cnt >= 64) {
                reject();
            } else {
                // 对所有位对检查整帧短/长簇的边界，避免首个抖动样本
                // 错当基准而拒绝后续有效位；无效段或位对仍立即拒绝。
                _s.shortMin = _s.cnt ? min(_s.shortMin, (uint16_t)sp) : sp;
                _s.shortMax = max(_s.shortMax, (uint16_t)sp);
                _s.longMin = _s.cnt ? min(_s.longMin, (uint16_t)lp) : lp;
                _s.longMax = max(_s.longMax, (uint16_t)lp);
                _s.data = (_s.data << 1) | (_s.teLast > dur ? 1 : 0);
                _s.cnt++; _s.shortSum += sp; _s.longSum += lp; _s.step = 1;
            }
        } else if (_s.step != 0) {
            reject();
        }
        return false;
    }
    bool hasFrame() const override { return _ready; }
    uint64_t frameData() const override { return _out; }
    uint8_t frameBits() const override { return _outBits; }
    uint16_t frameTe() const override { return _short; }
    uint16_t frameLongPulse() const override { return _long; }
    uint32_t frameGuard() const override { return _guard; }
    const char* protoName() const override { return "PWM"; }
    uint8_t protoId() const override { return 200; }
    uint8_t state() const override { return _s.step; }
    uint32_t guardNominal() const override { return 9600; }
    RFDecState saveState() const override { return _s; }
    void restoreState(const RFDecState& s) override { _s = s; _ready = false; }

private:
    static bool near(uint32_t actual, uint32_t expected) {
        // CC1101当前~3.79kBaud异步模式有约33us采样抖动，取40us包含
        // GPIO时间戳分辨率余量。慢速信号继续使用30%的相对容差。
        uint32_t tolerance = max((uint32_t)40, expected * 3 / 10);
        return expected && (actual > expected ? actual - expected : expected - actual) <= tolerance;
    }
    void reject() { _s.step = 0; _s.lastBits = 0; }
    RFDecState _s{};
    uint64_t _out = 0;
    uint8_t _outBits = 0;
    uint16_t _short = 0, _long = 0;
    uint32_t _guard = 0;
    bool _ready = false;
};
#endif
