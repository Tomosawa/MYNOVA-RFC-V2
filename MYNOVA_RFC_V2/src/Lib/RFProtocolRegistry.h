/*
  RFProtocolRegistry.h — Flipper 式射频协议注册中心（架构移植）

  参考：flipperzero-firmware-dev/lib/subghz/registry.c
        flipperzero-firmware-dev/lib/subghz/protocols/base.c

  设计要点（对比 Flipper 原版）：
  1. Flipper 用 C 函数指针 ops 表（alloc/feed/serialize/...），本工程是
     C++ 环境，改用抽象基类 IRFProtocolDecoder 表达同一套"统一协议接口"
     —— 增删协议只需实现接口 + 构造时 add() 注册，调用方（RFSignalDecoder）
     遍历注册表驱动所有协议机，协议间零耦合；
  2. 保留 Flipper 的 feed(level, duration) 流式段输入模型与状态机自由度，
     每个协议类内部状态任意，但必须支持 saveState/restoreState 快照
     （RFSignalDecoder 双极性试探时恢复现场，避免污染双帧确认状态）；
  3. 注册表为静态容量数组（无堆分配），解码器实例由宿主（RFSignalDecoder）
     作为成员持有，注册表只聚合指针 —— 与现有 RFProtocolDecoders.h 的
     静态成员风格一致，ISR/任务上下文均安全。
*/

#ifndef RF_PROTOCOL_REGISTRY_H
#define RF_PROTOCOL_REGISTRY_H

#include <Arduino.h>
#include <string.h>

// ── 通用解码器状态快照 ────────────────────────────────────────────────
// 所有协议机的可恢复内部状态投影到这个 POD 上（极性试探前后保存/恢复）。
// 字段对不同协议含义可以不同（如 teSum 仅 PT2262 风格协议用于 Te 反推），
// 只要协议类自己 save/restore 语义对称即可。
struct RFDecState {
    uint8_t  step;
    uint32_t teLast;
    uint64_t data;
    uint8_t  cnt;
    uint32_t teSum;
    uint64_t last;
    uint8_t  bits;   // CameStyle 风格存"输出帧位数"，PT2262 风格未用
    uint8_t  lastBits = 0; // 重复确认必须等长
    uint32_t shortSum = 0, longSum = 0; // 自适应 PWM 时序，不用于固定协议机
    uint16_t previousShort = 0, previousLong = 0;
    uint16_t shortMin = 0, shortMax = 0, longMin = 0, longMax = 0;
};

// ── 统一协议解码器接口 ────────────────────────────────────────────────
class IRFProtocolDecoder {
public:
    virtual ~IRFProtocolDecoder() = default;

    // 流式喂入一段电平：level=该段真实电平，duration=持续us
    // 返回 true 表示本段结束时新确认了一帧（双帧确认通过）
    virtual bool feed(bool level, uint32_t duration) = 0;

    virtual void reset() = 0;

    // 内部状态机步骤编号；0 约定为"等待帧锚点(Guard)"，
    // 只允许真实收到的 Guard 启动协议机。
    virtual uint8_t  state() const = 0;

    // 标称 Guard（帧头锚点）时长 us，供测试/协议时序查询
    virtual uint32_t guardNominal() const = 0;

    // 快照（极性试探不污染跨包双帧确认状态）
    virtual RFDecState saveState() const = 0;
    virtual void      restoreState(const RFDecState& s) = 0;

    // 帧结果查询
    virtual bool     hasFrame() const = 0;
    virtual uint64_t frameData() const = 0;
    virtual uint8_t  frameBits() const = 0;
    virtual uint16_t frameTe() const = 0;
    virtual uint16_t frameLongPulse() const { return frameTe() * 3; }
    virtual uint32_t frameGuard() const = 0;
    virtual const char* protoName() const = 0;
    virtual uint8_t  protoId() const = 0;
};

// ── 协议注册表 ────────────────────────────────────────────────────────
// 用法：宿主在构造函数中把自己的解码器成员逐个 add()；解码/复位时遍历。
// 容量静态分配，运行期无 malloc。
class RFProtocolRegistry {
public:
    static constexpr uint8_t MAX_DECODERS = 12;

    static RFProtocolRegistry& instance() {
        static RFProtocolRegistry reg;
        return reg;
    }

    bool add(IRFProtocolDecoder* dec) {
        if (dec == nullptr || _count >= MAX_DECODERS) return false;
        _dec[_count++] = dec;
        return true;
    }

    uint8_t count() const { return _count; }
    IRFProtocolDecoder* at(uint8_t i) { return (i < _count) ? _dec[i] : nullptr; }

    IRFProtocolDecoder* findByName(const char* name) {
        if (name == nullptr) return nullptr;
        for (uint8_t i = 0; i < _count; i++) {
            if (strcmp(_dec[i]->protoName(), name) == 0) return _dec[i];
        }
        return nullptr;
    }

    void resetAll() {
        for (uint8_t i = 0; i < _count; i++) _dec[i]->reset();
    }

    // 清空注册（宿主 RFSignalDecoder 重新构造时幂等重建注册表，
    // 保证注册表指针始终指向存活实例的协议机）
    void clear() { _count = 0; }

private:
    RFProtocolRegistry() : _count(0) { memset(_dec, 0, sizeof(_dec)); }
    IRFProtocolDecoder* _dec[MAX_DECODERS];
    uint8_t _count;
};

#endif
