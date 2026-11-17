/*
  FlipperSignalFile.h — Flipper .sub / .ir 信号文件解析与导出

  参考（格式定义源头）：
    .sub Key:   flipperzero-firmware-dev/lib/subghz/blocks/generic.c (serialize/deserialize)
    .sub RAW:   flipperzero-firmware-dev/lib/subghz/protocols/raw.c
    .ir:        flipperzero-firmware-dev/lib/infrared/signal/infrared_signal.c

  .sub Key File（协议型）：
    Filetype: Flipper SubGhz Key File
    Version: 1
    Frequency: 433920000
    Preset: FuriHalSubGhzPresetOok650Async
    Protocol: CAME
    Bit: 24
    Key: 00 00 00 00 12 34 56 78     ← 8 字节大端 hex = uint64
    TE: 390                           ← 部分协议携带（Princeton/SMC5326 等）

  .sub RAW File（原始录制）：
    Filetype: Flipper SubGhz RAW File
    Version: 1
    Frequency: 433920000
    Preset: FuriHalSubGhzPresetOok650Async
    RAW_Data: -320 640 -640 320 ...   ← 正=high（载波开），负=low

  .ir（红外，多帧，键为小写）：
    Filetype: Flipper Infrared File
    Version: 1
    name: Power
    type: raw | parsed
    ── raw: frequency / duty_cycle / data: <mark space mark ...>(us)
    ── parsed: protocol / address: 4字节hex(小端) / command: 4字节hex(小端)
    （每帧以新的 "name:" 键开始；支持 parsed 与 raw 混合）
*/

#ifndef FLIPPER_SIGNAL_FILE_H
#define FLIPPER_SIGNAL_FILE_H

#include <Arduino.h>
#include <FS.h>
#include "FlipperFormat.h"
#include "../Proto/SignalData.h"
#include "../Util/MemAlloc.h"
#include "RawCaptureLimits.h"

// .sub RAW 最大段数（覆盖长录制；Flipper 每行写 512 段，可有多行 RAW_Data）
// 上限在RawCaptureLimits.h统一定义；脉冲按需分配，优先PSRAM。
// .ir 单帧最大边沿数（与 IRSignalMeta 的 IR_MAX_FRAME_EDGES 一致）
#define IR_FILE_MAX_EDGES   512
// .ir 单文件最大帧数
#define IR_FILE_MAX_FRAMES  8

// ==================== .sub 解析结果 ====================
struct SubGhzKeyRecord {
    uint32_t frequencyHz;    // 0 = 无效记录
    char     preset[48];     // 录制时的调制预设，回放时使用同一预设
    char     protocol[24];   // CAME / Nice FLO / Holtek / Princeton / SMC5326 / RAW ...
    uint8_t  bits;           // 协议型位数
    uint64_t key;            // 协议型数据（8 字节大端）
    uint16_t te;             // 协议型 Te（文件无 TE 键时为 0 → 用协议常量）
    bool     isRaw;          // Protocol == "RAW"
    uint16_t rawCount;       // RAW 段数（rawData 有效元素数）
    int32_t* rawData = nullptr; // RAW_Data原值（±us），按需分配，不常驻内部RAM
    uint16_t rawCapacity = 0;
    bool reserveRaw(uint16_t count) {
        if (count > SUB_RAW_MAX_PULSES) return false;
        if (count <= rawCapacity) return true;
        uint16_t capacity = (uint16_t)((count + 1023u) & ~1023u);
        int32_t* next = static_cast<int32_t*>(anyMalloc(capacity * sizeof(int32_t)));
        if (!next) return false;
        if (rawCount) memcpy(next, rawData, rawCount * sizeof(int32_t));
        memFree(rawData);
        rawData = next;
        rawCapacity = capacity;
        return true;
    }
    SubGhzKeyRecord(const SubGhzKeyRecord&) = delete;
    SubGhzKeyRecord& operator=(const SubGhzKeyRecord&) = delete;
    ~SubGhzKeyRecord() { memFree(rawData); }

    SubGhzKeyRecord() { reset(); }
    void reset() {
        memFree(rawData); rawData = nullptr; rawCapacity = 0;
        frequencyHz = 0; bits = 0; key = 0; te = 0;
        isRaw = false; rawCount = 0;
        memset(preset, 0, sizeof(preset));
        memset(protocol, 0, sizeof(protocol));
    }
};

// ==================== .ir 解析结果 ====================
struct IrFileFrame {
    char     name[24];       // 按键名（Power/Vol+...）
    bool     isRaw;          // false = parsed（协议型）
    char     protocol[24];  // parsed: NEC / RC5 / Samsung ...（raw: 空或RAW）
    uint32_t address;       // parsed: 设备地址
    uint32_t command;       // parsed: 命令码
    uint32_t frequency;     // raw: 载波频率 Hz（默认 38000）
    uint16_t edgeCount;     // raw: 边沿数（mark/space 交替，首 mark）
    uint16_t* edges;        // raw: 边沿数组（us），loadIrFile 内部分配

    IrFileFrame() : isRaw(false), address(0), command(0),
                    frequency(38000), edgeCount(0), edges(nullptr) {
        memset(name, 0, sizeof(name));
        memset(protocol, 0, sizeof(protocol));
    }
};

struct IrFileRecord {
    uint8_t     frameCount;
    IrFileFrame frames[IR_FILE_MAX_FRAMES];

    IrFileRecord() : frameCount(0) {}
    // 释放所有帧的边沿内存（析构不自动释放，供调用方中途 abort）
    void destroy() {
        for (uint8_t i = 0; i < IR_FILE_MAX_FRAMES; i++) {
            if (frames[i].edges) { delete[] frames[i].edges; frames[i].edges = nullptr; }
        }
        frameCount = 0;
    }
};

// ==================== 加载 API ====================

// 解析 .sub 文件（协议型 / RAW 型统一入口）
// 返回 false：文件不存在 / 非 Flipper 格式 / 无有效数据
bool loadSubGhzFile(fs::FS& fs, const char* path, SubGhzKeyRecord& out);

// 解析 .ir 文件（多帧；raw 帧 edges 堆分配，调用方用 IrFileRecord::destroy() 释放）
// parsed 帧 isRaw=false（阶段一不支持协议重编码发射，仅保留解析信息）
bool loadIrFile(fs::FS& fs, const char* path, IrFileRecord& out);

// ==================== 导出 API ====================

// 把解码得到的原始脉冲段序列导出为 Flipper .sub RAW 文件（Flipper 可直接读）
// levels/durations: RFSignalDecoder 的 pulseLevels/pulses（段序列）
// presetName: Flipper Preset 名（nullptr 默认 Ook650Async）
bool saveSubGhzFileRAW(fs::FS& fs, const char* path, uint32_t frequencyHz,
                       const bool* levels, const uint16_t* durations, uint16_t count,
                       const char* presetName = nullptr);

// 已有有符号RAW序列直接保存，保留长间隔，不经uint16转换。
bool saveSubGhzFileRAWSigned(fs::FS& fs, const char* path, uint32_t frequencyHz,
                            const int32_t* pulses, uint16_t count, const char* presetName);

// 把协议型信号导出为 Flipper .sub Key 文件
// flipperProtoName: "Princeton"/"SMC5326"/"CAME"/"Nice FLO"/"Holtek" ...
bool saveSubGhzFileKey(fs::FS& fs, const char* path, uint32_t frequencyHz,
                       const char* flipperProtoName, uint8_t bits, uint64_t key, uint16_t te);

// 把设备 NVS 中的射频信号槽位导出为 .sub（协议型按协议名，其余走 RAW 需调用方提供脉冲）
// 协议名映射：PT2262/EV1527→Princeton、SMC5326→SMC5326、CAME/Nice FLO/Holtek 原名
const char* mynovaToFlipperProtoName(const char* mynovaProtoName);

// ==================== 槽位互转（.sub / .ir → 设备信号） ====================

// .sub 记录 → 设备射频槽位条目（可直接 DataStore::SaveSignal 或 SendData）
// 协议型仅支持本工程可发射的协议（Princeton/SMC5326/CAME/Nice FLO/Holtek）；
// RAW 记录无法转入槽位（槽位结构不支持任意脉冲），返回 false
bool subGhzRecordToSignalEntry(const SubGhzKeyRecord& rec, SignalEntry& entry);

// .ir 记录 → 设备红外槽位条目（raw 帧拼接为多帧边沿流，与红外学习数据同构）
// edgesOut 调用方提供（≥ 8×512×2B）；frameGaps 默认 100ms/帧。
// parsed 帧被跳过（阶段一不支持协议重编码）；全部帧均 parsed 时返回 false
bool irRecordToSignalEntry(const IrFileRecord& rec, SignalEntry& entry,
                           uint16_t* edgesOut, uint32_t edgesOutCount);

#endif
