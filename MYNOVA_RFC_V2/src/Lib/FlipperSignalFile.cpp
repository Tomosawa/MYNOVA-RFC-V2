/*
  FlipperSignalFile.cpp — Flipper .sub / .ir 信号文件解析与导出实现

  实现说明：
  - .sub：Filetype 校验接受 Key File 与 RAW File 两种；Protocol=="RAW" 时
       从 RAW_Data 解析 int32 数组（正=high，负=low，兼容空格/逗号分隔）；
       协议型读 Bit/Key(8字节大端hex)/TE。
  - .ir：以 "name:" 键开新帧；type:raw 收集 frequency/duty_cycle/data；
       type:parsed 收集 protocol/address/command（4字节hex，Flipper 按小端
       uint32 内存直存——这里按字节流重组为 uint32）。
  - 导出：RAW 与 Key 两种 .sub 写出，文件头/键名与 Flipper 原版逐字一致。
*/

#include "FlipperSignalFile.h"
#include "../Util/Log.h"
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

#define LOG_TAG "FFILE"

// 把 "00 01 12 34" 形式的字节串（字节间任意空白）依序填入 outBytes
// 返回解析出的字节数（maxBytes 上限）
static uint8_t parseHexBytes(const String& value, uint8_t* outBytes, uint8_t maxBytes) {
    uint8_t count = 0;
    const char* p = value.c_str();
    while (*p && count < maxBytes) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        char* end = nullptr;
        long b = strtol(p, &end, 16);
        if (end == p) break;           // 非法字符
        outBytes[count++] = (uint8_t)b;
        p = end;
    }
    return count;
}

// 把整数值数组（空格/逗号分隔，可带负号）填入 out，返回元素数
static uint16_t parseInt32Array(const String& value, int32_t* out, uint16_t maxCount) {
    uint16_t count = 0;
    const char* p = value.c_str();
    while (*p && count < maxCount) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        char* end = nullptr;
        long v = strtol(p, &end, 10);
        if (end == p) break;
        out[count++] = (int32_t)v;
        p = end;
    }
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────
// .sub 解析
// ─────────────────────────────────────────────────────────────────────────────
bool loadSubGhzFile(fs::FS& fs, const char* path, SubGhzKeyRecord& out) {
    out.reset();

    FlipperFormat ff;
    if (!ff.openRead(fs, path)) {
        LOG_E("[FFILE] open failed: %s", path);
        return false;
    }

    String key, value;
    bool filetypeOk = false;
    bool sawFrequency = false, sawProtocol = false;

    while (ff.next(key, value)) {
        if (key == "Filetype") {
            // Key File 与 RAW File 均为合法 .sub
            filetypeOk = (value == "Flipper SubGhz Key File" ||
                          value == "Flipper SubGhz RAW File");
            if (!filetypeOk) {
                LOG_E("[FFILE] bad Filetype: %s", value.c_str());
                return false;
            }
        } else if (key == "Frequency") {
            out.frequencyHz = strtoul(value.c_str(), nullptr, 10);
            sawFrequency = true;
        } else if (key == "Preset") {
            strncpy(out.preset, value.c_str(), sizeof(out.preset) - 1);
        } else if (key == "Protocol") {
            strncpy(out.protocol, value.c_str(), sizeof(out.protocol) - 1);
            sawProtocol = true;
        } else if (key == "Bit") {
            out.bits = (uint8_t)constrain(strtoul(value.c_str(), nullptr, 10), 0, 64);
        } else if (key == "Key") {
            // 8 字节 hex，高字节在前（大端 uint64）
            uint8_t bytes[8] = {0};
            uint8_t n = parseHexBytes(value, bytes, 8);
            if (n > 8) n = 8;
            uint64_t k = 0;
            for (uint8_t i = 0; i < n; i++) k = (k << 8) | bytes[i];
            out.key = k;
        } else if (key == "TE") {
            out.te = (uint16_t)constrain(strtoul(value.c_str(), nullptr, 10), 0, 60000);
        } else if (key == "RAW_Data") {
            // 逐项追加，多行RAW_Data不能覆盖；越界/损坏/内存不足明确失败。
            const char* token = value.c_str();
            while (*token) {
                while (*token == ' ' || *token == ',' || *token == '\t') ++token;
                if (!*token) break;
                char* end = nullptr;
                errno = 0;
                long long v = strtoll(token, &end, 10);
                if (end == token || errno == ERANGE || v < INT32_MIN || v > INT32_MAX ||
                    (*end && *end != ' ' && *end != ',' && *end != '\t') ||
                    out.rawCount >= SUB_RAW_MAX_PULSES || !out.reserveRaw(out.rawCount + 1)) {
                    LOG_E("[FFILE] invalid/oversized RAW or allocation failed: %s count=%u", path, out.rawCount);
                    out.reset();
                    return false;
                }
                out.rawData[out.rawCount++] = (int32_t)v;
                token = end;
            }
        }
        // 其余键（Manufacture/Repeat/Custom_preset_* 等）忽略
    }

    if (!filetypeOk || !sawFrequency || !sawProtocol) {
        LOG_E("[FFILE] %s: missing Filetype/Frequency/Protocol", path);
        return false;
    }

    out.isRaw = (strcmp(out.protocol, "RAW") == 0);
    if (out.isRaw) {
        if (out.rawCount < 8) {
            LOG_E("[FFILE] %s: RAW_Data too short (%u)", path, out.rawCount);
            return false;
        }
    } else {
        if (out.bits == 0 || out.key == 0) {
            LOG_E("[FFILE] %s: missing Bit/Key", path);
            return false;
        }
    }

    LOG_I("[FFILE] %s: %s %dbit/RAW%u f=%luHz",
          path, out.protocol, out.bits, out.rawCount, (unsigned long)out.frequencyHz);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// .ir 解析
// ─────────────────────────────────────────────────────────────────────────────
bool loadIrFile(fs::FS& fs, const char* path, IrFileRecord& out) {
    out.destroy();

    FlipperFormat ff;
    if (!ff.openRead(fs, path)) {
        LOG_E("[FFILE] open failed: %s", path);
        return false;
    }

    String key, value;
    bool filetypeOk = false;
    IrFileFrame* cur = nullptr;   // 当前帧（遇 name: 开新帧）

    while (ff.next(key, value)) {
        if (key == "Filetype") {
            filetypeOk = (value == "Flipper Infrared File");
            if (!filetypeOk) {
                LOG_E("[FFILE] bad ir Filetype: %s", value.c_str());
                return false;
            }
        } else if (key == "name") {
            if (out.frameCount >= IR_FILE_MAX_FRAMES) break;   // 帧满
            cur = &out.frames[out.frameCount++];
            strncpy(cur->name, value.c_str(), sizeof(cur->name) - 1);
        } else if (key == "type") {
            if (cur == nullptr) continue;
            cur->isRaw = (value == "raw");
        } else if (key == "protocol") {
            if (cur == nullptr) continue;
            strncpy(cur->protocol, value.c_str(), sizeof(cur->protocol) - 1);
        } else if (key == "address" || key == "command") {
            if (cur == nullptr) continue;
            // 4 字节 hex，Flipper 以小端内存直存 uint32：字节流重组
            uint8_t bytes[4] = {0};
            parseHexBytes(value, bytes, 4);
            uint32_t v = ((uint32_t)bytes[0]) | ((uint32_t)bytes[1] << 8) |
                         ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
            if (key == "address") cur->address = v; else cur->command = v;
        } else if (key == "frequency") {
            if (cur == nullptr) continue;
            cur->frequency = strtoul(value.c_str(), nullptr, 10);
        } else if (key == "duty_cycle") {
            // 载波占空比：发射链路固定 33%，忽略
        } else if (key == "data") {
            if (cur == nullptr || !cur->isRaw) continue;
            // 边沿数据量大：先粗算个数再分配（一次遍历避免浪费）
            uint16_t est = 1;
            for (unsigned int i = 0; i < value.length(); i++) {
                if (value[i] == ' ' || value[i] == ',') est++;
            }
            if (est > IR_FILE_MAX_EDGES) est = IR_FILE_MAX_EDGES;
            cur->edges = new uint16_t[est];
            // parseInt32Array 用 int32 缓冲，边沿全为正数且 ≤65535，
            // 直接以 int32 桥接读取
            int32_t* tmp = new int32_t[est];
            uint16_t n = parseInt32Array(value, tmp, est);
            for (uint16_t i = 0; i < n; i++) {
                cur->edges[i] = (uint16_t)constrain(tmp[i], 0, 65535);
            }
            delete[] tmp;
            cur->edgeCount = n;
        }
    }

    if (!filetypeOk || out.frameCount == 0) {
        LOG_E("[FFILE] %s: no valid ir frames", path);
        out.destroy();
        return false;
    }

    LOG_I("[FFILE] %s: %u ir frames", path, out.frameCount);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// .sub 导出
// ─────────────────────────────────────────────────────────────────────────────
bool saveSubGhzFileRAW(fs::FS& fs, const char* path, uint32_t frequencyHz,
                       const bool* levels, const uint16_t* durations, uint16_t count,
                       const char* presetName) {
    if (levels == nullptr || durations == nullptr || count == 0) return false;
    if (presetName == nullptr) presetName = "FuriHalSubGhzPresetOok650Async";

    FlipperFormat ff;
    if (!ff.openWrite(fs, path)) {
        LOG_E("[FFILE] write open failed: %s", path);
        return false;
    }

    bool ok = count <= SUB_RAW_MAX_PULSES &&
              ff.writeHeader("Flipper SubGhz RAW File", 1) &&
              ff.writeKV_U32("Frequency", frequencyHz) &&
              ff.writeKV("Preset", presetName) && ff.writeKV("Protocol", "RAW");
    int32_t chunk[256];
    for (uint16_t offset = 0; ok && offset < count;) {
        uint16_t n = min((uint16_t)256, (uint16_t)(count - offset));
        for (uint16_t i = 0; i < n; ++i) {
            chunk[i] = levels[offset + i] ? durations[offset + i] : -(int32_t)durations[offset + i];
        }
        ok = ff.writeKV_I32Array("RAW_Data", chunk, n);
        offset += n;
    }
    ff.close();
    uint16_t n = count;

    if (ok) {
        LOG_I("[FFILE] exported RAW: %s (%u pulses)", path, n);
    }
    return ok;
}

bool saveSubGhzFileRAWSigned(fs::FS& fs, const char* path, uint32_t frequencyHz,
                            const int32_t* pulses, uint16_t count, const char* presetName) {
    if (!pulses || count == 0 || count > SUB_RAW_MAX_PULSES) return false;
    FlipperFormat ff;
    if (!ff.openWrite(fs, path)) return false;
    bool ok = ff.writeHeader("Flipper SubGhz RAW File", 1) &&
              ff.writeKV_U32("Frequency", frequencyHz) &&
              ff.writeKV("Preset", presetName ? presetName : "FuriHalSubGhzPresetOok650Async") &&
              ff.writeKV("Protocol", "RAW");
    for (uint16_t offset = 0; ok && offset < count;) {
        uint16_t n = min((uint16_t)256, (uint16_t)(count - offset));
        ok = ff.writeKV_I32Array("RAW_Data", pulses + offset, n);
        offset += n;
    }
    return ok;
}

bool saveSubGhzFileKey(fs::FS& fs, const char* path, uint32_t frequencyHz,
                       const char* flipperProtoName, uint8_t bits, uint64_t key, uint16_t te) {
    if (flipperProtoName == nullptr || bits == 0) return false;

    FlipperFormat ff;
    if (!ff.openWrite(fs, path)) {
        LOG_E("[FFILE] write open failed: %s", path);
        return false;
    }

    bool ok = ff.writeHeader("Flipper SubGhz Key File", 1) &&
              ff.writeKV_U32("Frequency", frequencyHz) &&
              ff.writeKV("Preset", "FuriHalSubGhzPresetOok650Async") &&
              ff.writeKV("Protocol", flipperProtoName) &&
              ff.writeKV_U32("Bit", bits) &&
              ff.writeKV_Hex64("Key", key) &&
              (te == 0 || ff.writeKV_U32("TE", te));
    ff.close();

    if (ok) {
        LOG_I("[FFILE] exported Key: %s (%s %dbit)", path, flipperProtoName, bits);
    }
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// 协议名映射（设备名 ↔ Flipper 名）
// ─────────────────────────────────────────────────────────────────────────────
const char* mynovaToFlipperProtoName(const char* mynovaProtoName) {
    if (mynovaProtoName == nullptr) return nullptr;
    // PT2262/EV1527 家族在 Flipper 中统称 Princeton
    if (strcmp(mynovaProtoName, "PT2262") == 0 ||
        strcmp(mynovaProtoName, "EV1527") == 0) {
        return "Princeton";
    }
    if (strcmp(mynovaProtoName, "SMC5326") == 0) return "SMC5326";
    if (strcmp(mynovaProtoName, "CAME") == 0)     return "CAME";
    if (strcmp(mynovaProtoName, "Nice FLO") == 0) return "Nice FLO";
    if (strcmp(mynovaProtoName, "Holtek") == 0)   return "Holtek";
    return nullptr;   // 其余协议无 Flipper 对应 Key 格式（可导出 RAW）
}

// Flipper 协议名 → 设备协议名 + 默认 Te（协议文件无 TE 键时用）
static const char* flipperToMynovaProtoName(const char* flipperName, uint16_t& defTe) {
    if (flipperName == nullptr) return nullptr;
    if (strcmp(flipperName, "Princeton") == 0) { defTe = 390; return "PT2262"; }
    if (strcmp(flipperName, "SMC5326") == 0)   { defTe = 300; return "SMC5326"; }
    if (strcmp(flipperName, "CAME") == 0)      { defTe = 320; return "CAME"; }
    if (strcmp(flipperName, "Nice FLO") == 0)  { defTe = 700; return "Nice FLO"; }
    if (strcmp(flipperName, "Holtek") == 0)    { defTe = 430; return "Holtek"; }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// .sub → 设备射频槽位
// ─────────────────────────────────────────────────────────────────────────────
bool subGhzRecordToSignalEntry(const SubGhzKeyRecord& rec, SignalEntry& entry) {
    if (rec.isRaw || rec.bits == 0 || rec.frequencyHz == 0) return false;

    uint16_t defTe = 0;
    const char* mynovaName = flipperToMynovaProtoName(rec.protocol, defTe);
    if (mynovaName == nullptr) {
        LOG_E("[FFILE] proto not tx-capable: %s", rec.protocol);
        return false;
    }

    uint16_t te = (rec.te > 0) ? rec.te : defTe;
    bool isCameStyle = (strcmp(mynovaName, "CAME") == 0 ||
                       strcmp(mynovaName, "Nice FLO") == 0 ||
                       strcmp(mynovaName, "Holtek") == 0);

    entry.name = "";           // 由调用方命名
    entry.kind = SIG_RF;
    entry.rf.frequency = (float)rec.frequencyHz / 1000000.0f;
    entry.rf.bitCount   = rec.bits;
    entry.rf.shortPulse = te;
    entry.rf.longPulse  = isCameStyle ? (uint16_t)(te * 2) : (uint16_t)(te * 3);
    // PT2262 系帧尾 Guard 32Te（Flipper PT_GUARD_TIME），CameStyle 帧头 Guard 兼做
    entry.rf.syncPulse  = (uint16_t)(te * 32);
    strncpy(entry.rf.protocolName, mynovaName, sizeof(entry.rf.protocolName) - 1);
    entry.rf.protocolName[sizeof(entry.rf.protocolName) - 1] = '\0';

    // key 的 bits 位 MSB first 落入 data[32]
    memset(entry.rf.data, 0, sizeof(entry.rf.data));
    for (uint8_t i = 0; i < rec.bits && i < 64; i++) {
        if (rec.key & (1ULL << (rec.bits - 1 - i)))
            entry.rf.data[i / 8] |= (uint8_t)(1 << (7 - (i % 8)));
    }
    return entry.rf.isValid();
}

// ─────────────────────────────────────────────────────────────────────────────
// .ir → 设备红外槽位（raw 帧拼接，与红外学习数据同构）
// ─────────────────────────────────────────────────────────────────────────────
bool irRecordToSignalEntry(const IrFileRecord& rec, SignalEntry& entry,
                           uint16_t* edgesOut, uint32_t edgesOutCount) {
    if (edgesOut == nullptr || edgesOutCount < 32) return false;

    entry.name = "";
    entry.kind = SIG_IR;
    entry.ir = IRSignalMeta();

    uint32_t pos = 0;
    for (uint8_t f = 0; f < rec.frameCount && f < IR_MAX_FRAMES; f++) {
        const IrFileFrame& fr = rec.frames[f];
        if (!fr.isRaw || fr.edgeCount == 0 || fr.edges == nullptr) {
            LOG_D("[FFILE] ir frame '%s' parsed → skip (stage1)", fr.name);
            continue;
        }
        if (pos + fr.edgeCount > edgesOutCount) break;   // 边沿缓冲满

        memcpy(edgesOut + pos, fr.edges, fr.edgeCount * sizeof(uint16_t));
        uint8_t idx = entry.ir.frameCount;
        entry.ir.frameLens[idx]  = fr.edgeCount;
        entry.ir.frameGapsMs[idx] = (idx == 0) ? 0 : 100;   // 无文件信息，默认帧间隔
        strncpy(entry.ir.protocolName, "RAW", sizeof(entry.ir.protocolName) - 1);
        pos += fr.edgeCount;
        entry.ir.frameCount++;
    }

    if (entry.ir.frameCount == 0) return false;

    entry.ir.totalEdges = pos;
    return true;
}
