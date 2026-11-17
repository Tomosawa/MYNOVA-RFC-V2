/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef SIGNALDATA_H
#define SIGNALDATA_H

#include <Arduino.h>

// ==================== 信号类型 ====================
enum SignalKind {
    SIG_RF = 0,     // 无线射频信号（CC1101）
    SIG_IR = 1      // 红外信号
};

// ==================== 射频信号数据 ====================
// CC1101 接收/发射共用的 OOK 信号描述（与解码协议无关，直接以时序参数表达）
//
// RAW 脉冲保存在 TF 卡的 .sub 文件，旧槽位按 rawSlot 读取 LittleFS。
struct RFSignalData {
    float    frequency;            // 发射频率 (MHz)
    uint16_t bitCount;             // 位长度
    uint16_t shortPulse;           // 短脉冲宽度 (us)
    uint16_t longPulse;            // 长脉冲宽度 (us)
    uint16_t syncPulse;            // 同步脉冲宽度 (us)
    uint8_t  data[32];             // 数据（最多256位，MSB first）
    char     protocolName[24];     // 解码识别出的协议名（仅显示用，发射不依赖）

    // ── RAW 扩展（blob 尾部追加，旧数据读出为 0）──
    uint8_t  rawFlag;              // 1=RAW 录制信号（边车文件存储脉冲）
    char     rawPreset[24];        // 预设显示名；实际回放读取RAW边车完整Preset
    uint16_t rawCount;             // RAW 段数（显示用）
    uint64_t rawFileId;            // TF 文件 ID；0 表示旧 LittleFS 槽位
    uint8_t  rawSlot;              // 运行期字段：所在槽位 1..100（不入 blob，ReadSignal 后填）

    RFSignalData() : frequency(433.92f), bitCount(0),
                     shortPulse(0), longPulse(0), syncPulse(0),
                     rawFlag(0), rawCount(0), rawFileId(0), rawSlot(0) {
        memset(data, 0, sizeof(data));
        memset(protocolName, 0, sizeof(protocolName));
        memset(rawPreset, 0, sizeof(rawPreset));
    }

    // 判断是否为有效信号（有数据且有时序参数）
    bool isValid() const { return bitCount > 0 && shortPulse > 0; }

    // 取前4字节的码值（供旧界面/调试显示用）
    uint32_t getValue32() const {
        uint32_t v = 0;
        for (int i = 0; i < 4 && i < (int)((bitCount + 7) / 8); i++) {
            v = (v << 8) | data[i];
        }
        return v;
    }
};

// ==================== 红外信号数据（多帧） ====================
// 一次红外学习可能包含多个不同的帧（如空调遥控：长帧+跟随帧），
// 发射时需按顺序重放全部帧才等效于原始遥控操作。
#define IR_MAX_FRAMES       8     // 一次学习最多捕获帧数
#define IR_MAX_FRAME_EDGES  512   // 每帧最大边沿数（覆盖最大AC协议单帧≈511位）

// IRSignalMeta.frameProto 中 UNKNOWN 协议的存储值
// （decode_type_t::UNKNOWN = -1，序列化为uint8后为0xFF）
#define IR_PROTO_BYTE_UNKNOWN  0xFF

// 红外信号的元数据部分（边沿数据量大，由 DataStore 单独读写文件流）
//
// v2 扩展（存储格式带魔数+版本，旧数据自动兼容）:
//   新增每帧解码结果（协议/位宽/码值/地址/命令）与空调协议的人类可读
//   状态描述（如 "26C, COOL, FAN3"），供界面展示"具体值"使用。
//   发射链路不依赖这些字段——无解码详情的旧数据仍按边沿原始重放。
struct IRSignalMeta {
    uint8_t  frameCount;                      // 帧数量
    uint16_t frameLens[IR_MAX_FRAMES];        // 每帧边沿数
    uint16_t frameGapsMs[IR_MAX_FRAMES];      // 每帧与前一帧的间隔（ms，重放保真用）
    char     protocolName[24];                // 协议名（NEC/Samsung/RAW...）
    uint32_t totalEdges;                      // 全部帧边沿总数

    // ── v2: 每帧解码详情（旧数据全为0，界面显示"无解码详情"） ──
    uint8_t  frameBits[IR_MAX_FRAMES];        // 每帧解码位宽（bits）
    uint8_t  frameProto[IR_MAX_FRAMES];       // 每帧协议（decode_type_t枚举值）
    uint64_t frameValue[IR_MAX_FRAMES];       // 每帧解码码值（UNKNOWN帧为FNV指纹）
    uint32_t frameAddr[IR_MAX_FRAMES];        // 每帧设备地址（协议不提供时为0）
    uint32_t frameCmd[IR_MAX_FRAMES];         // 每帧命令码（协议不提供时为0）
    char     acStateDesc[64];                 // 空调协议可读状态（首个AC帧，非空调为空）

    IRSignalMeta() : frameCount(0), totalEdges(0) {
        memset(frameLens, 0, sizeof(frameLens));
        memset(frameGapsMs, 0, sizeof(frameGapsMs));
        memset(protocolName, 0, sizeof(protocolName));
        memset(frameBits, 0, sizeof(frameBits));
        memset(frameProto, 0, sizeof(frameProto));
        memset(frameValue, 0, sizeof(frameValue));
        memset(frameAddr, 0, sizeof(frameAddr));
        memset(frameCmd, 0, sizeof(frameCmd));
        memset(acStateDesc, 0, sizeof(acStateDesc));
    }
};

// ==================== 统一信号条目（存储/管理用） ====================
// 100个统一槽位，每个槽位要么是射频信号要么是红外信号；
// 红外边沿数据不随条目加载（发射时由 DataStore 按需读取）。
struct SignalEntry {
    String      name;      // 用户命名
    SignalKind  kind;      // 信号类型
    RFSignalData rf;       // kind==SIG_RF 时有效
    IRSignalMeta ir;       // kind==SIG_IR 时有效（仅元数据）

    SignalEntry() : kind(SIG_RF) {}
};

// ==================== 待保存信号草稿（接收/学习页 → 保存页） ====================
// 射频数据值拷贝；红外数据引用 IRHelper 内部缓冲
// （学习页与保存页同时在页面栈中，引用在保存完成前始终有效）
struct SignalDraft {
    SignalKind kind;
    RFSignalData rf;                  // kind==SIG_RF 时有效
    const IRSignalMeta* irMeta;       // kind==SIG_IR 时有效
    const uint16_t* irEdges;          // kind==SIG_IR 时有效

    // RAW 录制草稿：rf.rawFlag=1 时，脉冲在临时 .sub 文件中，保存时改名为边车
    bool   rawCapture;                // true=RAW 录制会话产物
    String rawTempPath;               // 临时文件路径（如 /raw_tmp.sub）

    SignalDraft() : kind(SIG_RF), irMeta(nullptr), irEdges(nullptr), rawCapture(false) {}
};

#endif
