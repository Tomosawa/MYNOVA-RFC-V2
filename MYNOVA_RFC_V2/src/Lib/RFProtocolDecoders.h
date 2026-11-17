/*
  RFProtocolDecoders.h — Flipper Zero 风格的独立协议解码器（移植）

  参考：flipperzero-firmware-dev/lib/subghz/protocols/princeton.c
        flipperzero-firmware-dev/lib/subghz/protocols/smc5326.c
        flipperzero-firmware-dev/lib/subghz/blocks/{const,decoder,generic,math}.h

  设计要点（与原 RFSignalDecoder 直方图+通用 PWM 分类器的区别）：
  1. 每个协议独立状态机，不估计 short/long，不算 ratio，不猜编码；
  2. 固定 Te 基准 + 非对称容差（short ±delta，long ±3*delta）；
  3. 必须看到 Guard 低电平（30~36Te）才开始收，位数必须 == needBits；
  4. 必须连续两帧 data 完全一致才上报（last_data 机制），23bit 截断帧
     在原理上无法输出，只能是“无输出”，不可能是“错误输出”；
  5. Te 用实测帧反推（total/(bits*4+1)），供发射链路精确重放。

  ISR 约定：pulses[i] 为某段电平的持续时间(us)，
            levels[i] 为该段的真实电平（true=High）。调用方必须保证
            存入的就是段电平，而不是边沿后的新电平（见 processInterrupt 注释）。
*/

#ifndef RF_PROTOCOL_DECODERS_H
#define RF_PROTOCOL_DECODERS_H

#include <Arduino.h>
#include <string.h>
#include "RFProtocolRegistry.h"

#ifndef DURATION_DIFF
#define DURATION_DIFF(a, b) ((a) < (b) ? (b) - (a) : (a) - (b))
#endif

// ── 协议时序常量（直接抄 Flipper 实测值） ──────────────────────────────
struct PWMProtoConst {
    uint16_t teShort;   // Te（短脉冲基准）
    uint16_t teLong;    // 3Te（长脉冲基准）
    uint16_t teDelta;   // 短脉冲容差；长脉冲容差 = 3*teDelta
    uint8_t  needBits;  // 必须凑齐的位数，少 1 位直接丢弃
    uint16_t guardTe;   // Guard 低电平 ≈ guardTe * Te
    uint16_t guardTol;  // Guard 容差 = teDelta * guardTol（照抄 Flipper 原文倍数）
};

// Princeton/PT2262/EV1527 家族：Te=390, Guard=36Te≈14ms，容差照抄 Flipper 原文
static const PWMProtoConst PROTO_PRINCETON = {390, 1170, 300, 24, 36, 36};
// EV1527 学习码（稍快）：Te=320, Guard=30Te≈9.6ms
static const PWMProtoConst PROTO_EV1527 = {320, 960, 200, 24, 30, 30};
// SMC5326（25bit）：Te=300, Guard=24Te≈7.2ms，容差 delta*12（抄 smc5326.c 原文）
static const PWMProtoConst PROTO_SMC5326 = {300, 900, 200, 25, 24, 12};

// ── 单协议流式解码器 ────────────────────────────────────────────────────
// 实现 IRFProtocolDecoder 统一接口，纳入 RFProtocolRegistry 注册中心管理
class PWMProtocolDecoder : public IRFProtocolDecoder {
public:
    PWMProtocolDecoder(const PWMProtoConst* c = &PROTO_PRINCETON,
                       const char* name = "PT2262", uint8_t id = 1)
        : _c(c), _name(name), _id(id) { reset(); }

    void rebind(const PWMProtoConst* c, const char* name, uint8_t id) {
        _c = c; _name = name; _id = id; reset();
    }

    void reset() override {
        _step = 0; _teLast = 0; _data = 0; _cnt = 0;
        _teSum = 0; _last = 0; _ready = false;
        _out = 0; _outTe = 0; _outGuard = 0;
    }

    // Flipper 状态机 Reset/Save/Check 的直接移植
    // level: 该段真实电平；dur: us。返回 true 表示本段结束时新确认一帧。
    bool feed(bool level, uint32_t dur) override {
        _ready = false;
        switch (_step) {
            case 0: {  // Reset：等 Guard 低电平
                uint32_t guard = (uint32_t)_c->teShort * _c->guardTe;
                uint32_t tol = (uint32_t)_c->teDelta * _c->guardTol;
                if (!level && DURATION_DIFF(dur, guard) < tol) {
                    _step = 1; _data = 0; _cnt = 0; _teSum = 0;
                }
                break;
            }
            case 1:  // SaveDuration：存 High 段
                if (level) {
                    _teLast = dur; _teSum += dur; _step = 2;
                } else {
                    _last = 0;
                    _step = 0;  // Guard 后必须是 High，否则回 Reset
                }
                break;
            case 2:  // CheckDuration：Low 段到来，与 teLast 配对判 bit
                if (!level) {
                    // 帧结束：超长 Low（>= 2*teLong）
                    if (dur >= (uint32_t)_c->teLong * 2) {
                        if (_cnt == _c->needBits) {
                            // 双帧确认：本次 == 上次才上报
                            if (_last == _data && _last != 0) {
                                _out = _data;
                                // Flipper Te 反推：total/(bits*4+1)
                                uint32_t denom = (uint32_t)_cnt * 4 + 1;
                                _outTe = denom ? (uint16_t)(_teSum / denom) : _c->teShort;
                                if (_outTe < 100) _outTe = _c->teShort;
                                if (_outTe > 2000) _outTe = _c->teShort;
                                _outGuard = dur;  // trailing guard 实测值，供发射重放
                                _ready = true;
                            }
                            _last = _data;
                        } else {
                            _last = 0; // 截断帧打断重复确认
                        }
                        _data = 0; _cnt = 0; _teSum = 0;
                        _step = 1;  // Flipper 回到 Save，等下一帧 High
                        break;
                    }

                    _teSum += dur;

                    bool is0 = DURATION_DIFF(_teLast, _c->teShort) < _c->teDelta &&
                               DURATION_DIFF(dur, _c->teLong) < (uint32_t)_c->teDelta * 3;
                    bool is1 = DURATION_DIFF(_teLast, _c->teLong) < (uint32_t)_c->teDelta * 3 &&
                               DURATION_DIFF(dur, _c->teShort) < _c->teDelta;
                    if (is0) {
                        _data = (_data << 1) | 0; _cnt++; _step = 1;
                    } else if (is1) {
                        _data = (_data << 1) | 1; _cnt++; _step = 1;
                    } else {
                        _last = 0;
                        _step = 0;  // 配对失败，重复确认失效
                    }
                    // 溢出保护：bit 数超过 needBits+4 说明已错位，直接丢
                    if (_cnt > _c->needBits + 4) {
                        _last = 0;
                        _data = 0; _cnt = 0; _teSum = 0; _step = 0;
                    }
                } else {
                    _last = 0;
                    _step = 0;
                }
                break;
        }
        return _ready;
    }

    bool     hasFrame() const override { return _ready; }
    uint64_t frameData() const override { return _out; }
    uint8_t  frameBits() const override { return _c->needBits; }
    uint16_t frameTe() const override { return _outTe ? _outTe : _c->teShort; }
    uint32_t frameGuard() const override { return _outGuard; }
    const char* protoName() const override { return _name; }
    uint8_t  protoId() const override { return _id; }
    uint8_t  state() const override { return _step; }
    uint32_t guardNominal() const override { return (uint32_t)_c->teShort * _c->guardTe; }

    // 快照：供调用方在"试另一种极性"前后保存/恢复内部状态，
    // 避免试探污染双帧确认用的 last_data 与收帧进度
    RFDecState saveState() const override {
        RFDecState s;
        s.step = _step; s.teLast = _teLast; s.data = _data;
        s.cnt = _cnt; s.teSum = _teSum; s.last = _last; s.bits = 0;
        return s;
    }
    void restoreState(const RFDecState& s) override {
        _step = s.step; _teLast = s.teLast; _data = s.data;
        _cnt = s.cnt; _teSum = s.teSum; _last = s.last; _ready = false;
    }

private:
    const PWMProtoConst* _c;
    const char* _name;
    uint8_t  _id;
    uint8_t  _step;      // 0=Reset 1=Save 2=Check
    uint32_t _teLast;    // Save 段存的 High 时长
    uint64_t _data;      // 当前帧累积
    uint8_t  _cnt;       // 当前帧位数
    uint32_t _teSum;     // 当前帧 High+Low 累加，供 Te 反推
    uint64_t _last;      // 上一帧 data（双帧确认用）
    bool     _ready;
    uint64_t _out;
    uint16_t _outTe;
    uint32_t _outGuard;
};

#endif
