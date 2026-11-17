/*
  RFProtocolCameStyle.h — Flipper 协议移植：CAME / Nice Flo / Holtek

  来源（逐行对照移植，去掉 furi 依赖）：
    flipperzero-firmware-dev/lib/subghz/protocols/came.c
    flipperzero-firmware-dev/lib/subghz/protocols/nice_flo.c
    flipperzero-firmware-dev/lib/subghz/protocols/holtek.c

  三个协议在 Flipper 中状态机结构完全一致（仅常量与"上报条件"不同）：
    Reset        等 Guard：low 段 ≈ guardMul×Te，容差 guardTolMul×delta
    FoundStartBit 等 start bit：high 段 ≈ Te（Nice Flo / Holtek 遇 low 停留）
    SaveDuration  存 low 段；若 low ≥ frameEndUs 则本帧结束→判定上报
    CheckDuration high 段与 teLast 配对判 bit：
                  teLast≈Te &  dur≈teLong → 0；teLast≈teLong & dur≈Te → 1

  与 PT2262 风格（RFProtocolDecoders.h）的本质差异：
  1. 数据位极性相反：PT2262 是 (high, low) 对，CameStyle 是 (low, high) 对；
  2. 有 start bit（high 1×Te），PT2262 无；
  3. teLong = 2×Te（PT2262 家族是 3×Te）；
  4. 帧间锚点是帧头长 low Guard（PT2262 是帧尾 sync low）。
  因此不能复用 PWMProtocolDecoder，独立状态机。

  本工程加固（Flipper 原版没有）：
  - 双帧确认：连续两帧 data 一致才上报（回放单包噪声环境的防误报，
    与 PWMProtocolDecoder 同策略）；
  - 位数上限熔断：cnt 超过 accept 上限 + 8 视为错位，整帧丢弃；
  - saveState/restoreState：极性试探不污染 last_data。

  发射（buildFrame）：Flipper encoder 的忠实移植，输出 (level,duration)
  段序列，经 CC1101Driver::transmitPulses 发射。帧头 Guard 长度按 Flipper
  规则随位数变化（CAME 24/42bit=76Te、12/18bit=47Te、25bit=36Te、其余 16Te；
  Nice Flo / Holtek 固定 36Te），Guard 本身是长 low，兼做帧间间隔。
*/

#ifndef RF_PROTOCOL_CAME_STYLE_H
#define RF_PROTOCOL_CAME_STYLE_H

#include <Arduino.h>
#include <string.h>
#include "RFProtocolRegistry.h"

#ifndef DURATION_DIFF
#define DURATION_DIFF(a, b) ((a) < (b) ? (b) - (a) : (a) - (b))
#endif

// ── CameStyle 协议常量（照抄 Flipper 实测值） ────────────────────────
enum CameStyleAcceptMode {
    CAME_ACCEPT_LIST = 0,   // 位数 ∈ acceptBits 列表
    CAME_ACCEPT_MIN  = 1,   // 位数 ≥ minBits
    CAME_ACCEPT_HDR  = 2    // 位数 == minBits 且 (data & headerMask) == headerVal
};

struct CameStyleProtoConst {
    const char* name;
    uint8_t  id;
    uint16_t teShort, teLong, teDelta;
    uint16_t guardMul;     // Guard = teShort × guardMul
    uint16_t guardTolMul;  // Guard 容差 = teDelta × guardTolMul
    uint32_t frameEndUs;   // Save 段 low ≥ 此值 → 帧结束
    uint16_t longTolMul;   // bit 判定中长脉冲容差 = teDelta × longTolMul
    uint8_t  acceptMode;
    uint8_t  acceptBits[6]; // CAME_ACCEPT_LIST 用，0 结尾
    uint8_t  minBits;      // CAME_ACCEPT_MIN：下限；CAME_ACCEPT_HDR：精确位数
    uint64_t headerMask;   // CAME_ACCEPT_HDR 用（0 = 不校验）
    uint64_t headerVal;
};

// CAME：te=320/640/150，Guard 56Te±63δ，帧尾 low≥1280us，位数 {12,18,24,25,42}
static const CameStyleProtoConst PROTO_CAME = {
    "CAME", 101,
    320, 640, 150,
    56, 63,
    320u * 4u,
    1,
    CAME_ACCEPT_LIST, {12, 18, 24, 25, 42, 0}, 0, 0, 0
};

// Nice Flo：te=700/1400/200，Guard 36Te±36δ，帧尾 low≥2800us，位数 ≥12
// （协议名与 Flipper SUBGHZ_PROTOCOL_NICE_FLO_NAME 一致，.sub Protocol 精确匹配）
static const CameStyleProtoConst PROTO_NICE_FLO = {
    "Nice FLO", 102,
    700, 1400, 200,
    36, 36,
    700u * 4u,
    1,
    CAME_ACCEPT_MIN, {0}, 12, 0, 0
};

// Holtek HT6P20：te=430/870/100，Guard 36Te±36δ，帧尾 low≥10Te+δ，
// 位数 ==40 且 (data & 0xF000000000)==0x5000000000
// （协议名与 Flipper SUBGHZ_PROTOCOL_HOLTEK_NAME 一致）
static const CameStyleProtoConst PROTO_HOLTEK = {
    "Holtek", 103,
    430, 870, 100,
    36, 36,
    430u * 10u + 100u,
    2,
    CAME_ACCEPT_HDR, {0}, 40, 0xF000000000ULL, 0x5000000000ULL
};

// ── CameStyle 单协议流式解码器 ────────────────────────────────────────
class CameStyleProtocolDecoder : public IRFProtocolDecoder {
public:
    explicit CameStyleProtocolDecoder(const CameStyleProtoConst* c)
        : _c(c) { reset(); }

    void reset() override {
        _step = 0; _teLast = 0; _data = 0; _cnt = 0; _last = 0; _lastBits = 0;
        _ready = false; _out = 0; _outBits = 0; _outGuard = 0;
    }

    bool feed(bool level, uint32_t dur) override {
        _ready = false;
        switch (_step) {
            case 0: {  // Reset：等 Guard（low 长段）
                uint32_t guard = (uint32_t)_c->teShort * _c->guardMul;
                uint32_t tol   = (uint32_t)_c->teDelta * _c->guardTolMul;
                if (!level && DURATION_DIFF(dur, guard) < tol) {
                    _step = 1; _data = 0; _cnt = 0;
                }
                break;
            }
            case 1:  // FoundStartBit：等 start bit（high ≈ Te）
                if (!level) break;                    // low 段：停留等待
                if (DURATION_DIFF(dur, _c->teShort) < _c->teDelta) {
                    _data = 0; _cnt = 0; _step = 2;    // start bit 命中，清帧
                } else {
                    _last = 0; _lastBits = 0;
                    _step = 0;
                }
                break;
            case 2: {  // SaveDuration：存 low 段；超长 low → 帧结束
                if (!level) {
                    if (dur >= _c->frameEndUs) {
                        // 本帧结束：按协议规则判定是否上报
                        if (acceptFrame()) {
                            // 双帧确认：本次 == 上次才上报（防单帧噪声误报）
                            if (_last == _data && _last != 0 && _lastBits == _cnt) {
                                _out     = _data;
                                _outBits = _cnt;
                                _outGuard = dur;   // 帧尾实测 low，供重放参考
                                _ready   = true;
                            }
                            _last = _data;
                            _lastBits = _cnt;
                        } else {
                            _last = 0; _lastBits = 0;
                        }
                        _data = 0; _cnt = 0;
                        _step = 1;  // 回 FoundStartBit 等下一帧 start bit
                        break;
                    }
                    _teLast = dur; _step = 3;
                } else {
                    _last = 0; _lastBits = 0;
                    _step = 0;      // Save 期望 low，来了 high → 回 Reset
                }
                break;
            }
            case 3: {  // CheckDuration：high 段与 teLast 配对判 bit
                if (level) {
                    uint32_t longTol = (uint32_t)_c->teDelta * _c->longTolMul;
                    bool is0 = DURATION_DIFF(_teLast, _c->teShort) < _c->teDelta &&
                               DURATION_DIFF(dur,    _c->teLong)  < longTol;
                    bool is1 = DURATION_DIFF(_teLast, _c->teLong)  < longTol &&
                               DURATION_DIFF(dur,    _c->teShort) < _c->teDelta;
                    if (is0 || is1) {
                        _data = (_data << 1) | (is1 ? 1 : 0); _cnt++; _step = 2;
                    } else {
                        _last = 0; _lastBits = 0;
                        _step = 0;
                    }
                    // 熔断：位数超出协议上限+8 视为错位，整帧丢弃
                    uint8_t hardCap = maxAcceptBits() + 8;
                    if (_cnt > hardCap) { _data = 0; _cnt = 0; _step = 0; _last = 0; _lastBits = 0; }
                } else {
                    _last = 0; _lastBits = 0;
                    _step = 0;
                }
                break;
            }
        }
        return _ready;
    }

    // ── IRFProtocolDecoder 接口其余部分 ─────────────────────────────
    uint8_t  state() const override { return _step; }
    uint32_t guardNominal() const override {
        return (uint32_t)_c->teShort * _c->guardMul;
    }

    RFDecState saveState() const override {
        RFDecState s;
        s.step = _step; s.teLast = _teLast; s.data = _data;
        s.cnt = _cnt; s.teSum = 0; s.last = _last; s.bits = _outBits; s.lastBits = _lastBits;
        return s;
    }
    void restoreState(const RFDecState& s) override {
        _step = s.step; _teLast = s.teLast; _data = s.data;
        _cnt = s.cnt; _last = s.last; _outBits = s.bits; _lastBits = s.lastBits; _ready = false;
    }

    bool     hasFrame() const override { return _ready; }
    uint64_t frameData() const override { return _out; }
    uint8_t  frameBits() const override { return _outBits; }
    uint16_t frameTe() const override { return _c->teShort; }
    uint16_t frameLongPulse() const override { return _c->teLong; }
    uint32_t frameGuard() const override { return _outGuard; }
    const char* protoName() const override { return _c->name; }
    uint8_t  protoId() const override { return _c->id; }

    const CameStyleProtoConst* constSpec() const { return _c; }

    // ── 发射帧构造（Flipper encoder 移植） ───────────────────────────
    // 输出一帧的 (level,duration) 段序列：
    //   [帧头 Guard low] + [start bit high Te] + bits×(low,high 对)
    // bit1 = (low teLong, high teShort)；bit0 = (low teShort, high teLong)
    // te 可传 0 表示用协议常量 Te。返回段数；缓冲区需 ≥ 2*bits+2。
    static uint16_t buildFrame(const CameStyleProtoConst* c, uint64_t key,
                               uint8_t bits, uint16_t te,
                               uint16_t* durations, bool* levels) {
        if (c == nullptr || bits == 0 || bits > 64 ||
            durations == nullptr || levels == nullptr) return 0;
        if (te == 0) te = c->teShort;
        uint16_t n = 0;
        // 帧头 Guard：长 low，长度按 Flipper 规则随位数变化（兼做帧间隔）
        uint32_t guardUs = headerUsFor(c, bits);
        durations[n] = (guardUs > 65535) ? 65535 : (uint16_t)guardUs;
        levels[n] = false; n++;
        // start bit：high 1×Te
        durations[n] = te; levels[n] = true; n++;
        // 数据位 MSB first
        for (uint8_t i = bits; i > 0; i--) {
            bool b = (key >> (i - 1)) & 1;
            durations[n] = b ? c->teLong : c->teShort; levels[n] = false; n++;
            durations[n] = b ? c->teShort : c->teLong; levels[n] = true;  n++;
        }
        return n;
    }

    // 按 Flipper encoder 规则取帧头 Guard 长度：
    // CAME: 24/42bit=76Te(24320us)、12/18bit=47Te(15040us)、25bit=36Te(11520us)、
    //       其他=16Te；Nice Flo / Holtek: 36Te
    static uint32_t headerUsFor(const CameStyleProtoConst* c, uint8_t bits) {
        uint16_t mul;
        if (c == &PROTO_CAME) {
            switch (bits) {
                case 24: case 42: mul = 76; break;
                case 12: case 18: mul = 47; break;
                case 25:          mul = 36; break;
                default:          mul = 16; break;
            }
        } else {
            mul = 36;
        }
        return (uint32_t)c->teShort * mul;
    }

private:
    // 帧结束时的上报条件判定（三种协议各自规则）
    bool acceptFrame() const {
        switch (_c->acceptMode) {
            case CAME_ACCEPT_LIST: {
                for (uint8_t i = 0; i < 6 && _c->acceptBits[i] != 0; i++) {
                    if (_cnt == _c->acceptBits[i]) return true;
                }
                return false;
            }
            case CAME_ACCEPT_MIN:
                return _cnt >= _c->minBits;
            case CAME_ACCEPT_HDR:
                return _cnt == _c->minBits &&
                       (_data & _c->headerMask) == _c->headerVal;
        }
        return false;
    }

    uint8_t maxAcceptBits() const {
        switch (_c->acceptMode) {
            case CAME_ACCEPT_LIST: {
                uint8_t m = 0;
                for (uint8_t i = 0; i < 6 && _c->acceptBits[i] != 0; i++) {
                    if (_c->acceptBits[i] > m) m = _c->acceptBits[i];
                }
                return m;
            }
            case CAME_ACCEPT_MIN: return 42;  // Nice Flo 熔断上限（实际设备 ≤24）
            case CAME_ACCEPT_HDR: return _c->minBits;
        }
        return 64;
    }

    const CameStyleProtoConst* _c;
    uint8_t  _step;       // 0=Reset 1=FoundStartBit 2=Save 3=Check
    uint32_t _teLast;     // Save 段存的 low 时长
    uint64_t _data;       // 当前帧累积
    uint8_t  _cnt;        // 当前帧位数
    uint8_t  _lastBits;   // 上一完整帧长度
    uint64_t _last;       // 上一帧 data（双帧确认）
    bool     _ready;
    uint64_t _out;
    uint8_t  _outBits;
    uint32_t _outGuard;
};

#endif
