/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#include "DataStore.h"
#include "RawSignalFiles.h"
#include "../Util/MemAlloc.h"
#include <Preferences.h>
#include <LittleFS.h>
#include "../Lib/RawCaptureSession.h"
#include "../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "DATA"

// ==================== NVS 配置存储键定义 ====================
#define KEY_NAMESPACE       DataStore::SettingsNamespace          // 系统配置/快捷键/主页配置命名空间
#define KEY_SIGNAL_NS       DataStore::SignalsNamespace         // 信号专用命名空间（射频/红外统一槽位）
#define KEY_HPROF_ACTIVE    "HPROF_ACT"          // 当前激活主页 id (u8)
#define KEY_HPROF_PREFIX    "HPROF"              // 主页 blob 键前缀，完整键为 HPROF0..7
#define KEY_HPROF_VER       "HPROF_VER"
#define KEY_QUICKKEY_1  "QKEY_1"
#define KEY_QUICKKEY_2  "QKEY_2"
#define KEY_QUICKKEY_3  "QKEY_3"
#define KEY_QUICKKEY_4  "QKEY_4"
#define KEY_QUICKKEY_5  "QKEY_5"
#define KEY_QUICKKEY_6  "QKEY_6"
#define KEY_QUICKKEY_7  "QKEY_7"
#define KEY_QUICKKEY_8  "QKEY_8"
#define KEY_QUICKKEY_9  "QKEY_9"
#define KEY_SOUND_ALERT_ENABLE "SOUND_ALERT"
#define KEY_REPEAT_TRANSMIT "REPEAT_TRANSMIT"
#define KEY_TX_POWER_LEVEL "TX_POWER"
#define KEY_RX_SENSITIVITY "RX_SENS"
#define KEY_BRIGHTNESS "BRIGHTNESS"
#define KEY_AUTO_SLEEP_TIME "AUTO_SLEEP"
#define KEY_AUTO_SCREEN_OFF_TIME "AUTO_SCR_OFF"
#define KEY_AP_ENABLED "AP_ENABLED"
#define KEY_WIFI_ENABLED "WIFI_ENABLED"
#define KEY_AP_NAME "AP_NAME"
#define KEY_AP_PASSWORD "AP_PASSWORD"
#define KEY_WIFI_NAME "WIFI_NAME"
#define KEY_WIFI_PASSWORD "WIFI_PASSWORD"
#define KEY_BOOT_ANIM "BOOT_ANIM"

// MQTT配置专用命名空间
#define MQTT_NAMESPACE DataStore::MqttNamespace
#define MQTT_KEY_MODE "mode"
#define MQTT_KEY_SERVER "server"
#define MQTT_KEY_PORT "port"
#define MQTT_KEY_USERNAME "username"
#define MQTT_KEY_PASSWORD "password"

// ==================== 信号blob存储格式（NVS） ====================
// 每个槽位一个blob键 "rf_N"/"ir_N"（N=1..100，RF/IR独立），内容为紧凑二进制序列：
//   u8  kind                       信号类型 SIG_RF/SIG_IR
//   u8  nameLen + name[nameLen]    名称（最长31字节）
//   RF: float frequency + u16 bitCount/short/long/sync + char protocolName[24]
//       + u8 data[32]
//       RAW 扩展（v3，仅 rawFlag=1 时尾部追加，旧 blob 无此块读出为 0）:
//       u8 rawFlag + char rawPreset[24] + u16 rawCount = 27 字节
//       可选 u64 TF 文件 ID；没有该字段时读取旧 LittleFS 槽位。
//   IR v1（历史格式，按旧逻辑解析）:
//       char protocolName[24] + u8 frameCount + u16 frameLens[8]
//       + u16 frameGapsMs[8] + u32 totalEdges + u16 edges[totalEdges]
//   IR v2（当前格式，魔数0xA5+版本字节开头，向后兼容）:
//       u8 0xA5 + u8 version(=2)
//       + char protocolName[24] + u8 frameCount + u16 frameLens[8]
//       + u16 frameGapsMs[8] + u32 totalEdges
//       + u8 frameBits[8] + u8 frameProto[8] + u64 frameValue[8]
//       + u32 frameAddr[8] + u32 frameCmd[8] + char acStateDesc[64]
//       + u16 edges[totalEdges]
//   解析时首字节为0xA5走v2（解码详情可用），否则按v1（详情字段置零，
//   边沿原始重放功能完全不受影响）。
// 红外典型大小: 空调遥控2帧约1.2KB, 上限(8帧x512边沿)约6KB, 在NVS blob能力内
// 旧格式 "sig_N"（RF/IR共用）在首次启动时自动迁移到 rf_/ir_ 并清理。
#define SIGNAL_KEY_LEN     12
#define SIGNAL_NAME_MAX    31

// 信号键生成（RF/IR独立）
static void signalKeyForKind(SignalKind kind, int index, char* out, size_t sz) {
    snprintf(out, sz, "%s_%d", kind == SIG_RF ? "rf" : "ir", index);
}

// IR blob v2 标识与版本（见上方格式说明）
#define IR_BLOB_MAGIC       0xA5
#define IR_BLOB_VERSION     2

// NVS全局共享单例（仅DataStore内部使用，依赖互斥锁串行化）
static Preferences preferences;

// MQTT模式与字符串转换辅助函数
static String mqttModeToString(MQTTMode mode) {
    switch (mode) {
        case MQTTMode::AUTO:
            return "auto";
        case MQTTMode::MANUAL:
            return "manual";
        default:
            return "auto";
    }
}

static MQTTMode stringToMQTTMode(const String& mode) {
    if (mode == "manual") {
        return MQTTMode::MANUAL;
    }
    return MQTTMode::AUTO;
}

DataStore::DataStore()
{
    // 创建互斥锁，并检查是否创建成功
    preferencesMutex = xSemaphoreCreateRecursiveMutex();
    if (preferencesMutex == nullptr) {
        LOG_E("DataStore: 互斥锁创建失败!");
    }
}

DataStore::~DataStore()
{
    // 删除互斥锁
    if (preferencesMutex != nullptr) {
        vSemaphoreDelete(preferencesMutex);
        preferencesMutex = nullptr;
    }
}

// ==================== 信号blob序列化/反序列化 ====================

// 序列化信号条目到缓冲区，返回写入字节数（失败返回0）
int DataStore::serializeSignal(const SignalEntry& entry, const uint16_t* irEdges,
                               uint8_t* buf, int bufSize) {
    // 计算需要的总大小
    int nameLen = entry.name.length();
    if (nameLen > SIGNAL_NAME_MAX) nameLen = SIGNAL_NAME_MAX;

    int payloadSize = 0;
    if (entry.kind == SIG_RF) {
        payloadSize = 4 + 2 * 4 + 24 + 32;   // freq + 4个u16 + 协议名 + 数据
        if (entry.rf.rawFlag) payloadSize += 27 + (entry.rf.rawFileId ? sizeof(uint64_t) : 0);
    } else {
        // IR v2: 魔数1+版本1 + 头部61(协议名24+帧数1+帧长16+间隔16+总边沿4)
        //        + 解码详情208(位宽8+协议8+码值64+地址32+命令32+AC描述64)
        //        + 边沿数据
        payloadSize = 2 + 61 + (8 + 8 + 64 + 32 + 32 + 64)
                      + (int)entry.ir.totalEdges * 2;
    }
    int total = 2 + nameLen + payloadSize;
    if (buf == nullptr || total > bufSize) return 0;

    int off = 0;
    buf[off++] = (uint8_t)entry.kind;
    buf[off++] = (uint8_t)nameLen;
    memcpy(&buf[off], entry.name.c_str(), nameLen);
    off += nameLen;

    if (entry.kind == SIG_RF) {
        memcpy(&buf[off], &entry.rf.frequency, 4); off += 4;
        uint16_t fields[4] = { entry.rf.bitCount, entry.rf.shortPulse,
                               entry.rf.longPulse, entry.rf.syncPulse };
        memcpy(&buf[off], fields, 8); off += 8;
        memcpy(&buf[off], entry.rf.protocolName, 24); off += 24;
        memcpy(&buf[off], entry.rf.data, 32); off += 32;
        // RAW 尾部块（仅 rawFlag=1）
        if (entry.rf.rawFlag) {
            buf[off++] = entry.rf.rawFlag;
            memcpy(&buf[off], entry.rf.rawPreset, 24); off += 24;
            uint16_t cnt = entry.rf.rawCount;
            memcpy(&buf[off], &cnt, 2); off += 2;
            if (entry.rf.rawFileId) {
                memcpy(&buf[off], &entry.rf.rawFileId, sizeof(entry.rf.rawFileId));
                off += sizeof(entry.rf.rawFileId);
            }
        }
    } else {
        // IR v2: 魔数+版本 + v1头部 + 解码详情 + 边沿
        buf[off++] = IR_BLOB_MAGIC;
        buf[off++] = IR_BLOB_VERSION;
        memcpy(&buf[off], entry.ir.protocolName, 24); off += 24;
        buf[off++] = entry.ir.frameCount;
        memcpy(&buf[off], entry.ir.frameLens, 16); off += 16;
        memcpy(&buf[off], entry.ir.frameGapsMs, 16); off += 16;
        memcpy(&buf[off], &entry.ir.totalEdges, 4); off += 4;
        // 解码详情（v2新增）
        memcpy(&buf[off], entry.ir.frameBits, 8); off += 8;
        memcpy(&buf[off], entry.ir.frameProto, 8); off += 8;
        memcpy(&buf[off], entry.ir.frameValue, 64); off += 64;
        memcpy(&buf[off], entry.ir.frameAddr, 32); off += 32;
        memcpy(&buf[off], entry.ir.frameCmd, 32); off += 32;
        memcpy(&buf[off], entry.ir.acStateDesc, 64); off += 64;
        if (entry.ir.totalEdges > 0 && irEdges != nullptr) {
            uint32_t edgeBytes = entry.ir.totalEdges * 2;
            memcpy(&buf[off], irEdges, edgeBytes); off += (int)edgeBytes;
        }
    }
    return off;
}

// 从缓冲区反序列化信号条目（irEdges可为null: 仅解析元数据）
bool DataStore::deserializeSignal(const uint8_t* buf, int len,
                                  SignalEntry& entry, uint16_t* irEdges, uint32_t edgeBufCount) {
    if (buf == nullptr || len < 3) return false;
    int off = 0;

    entry = SignalEntry();
    entry.kind = (buf[off] == SIG_IR) ? SIG_IR : SIG_RF;
    off++;

    int nameLen = buf[off++];
    if (nameLen > SIGNAL_NAME_MAX || off + nameLen > len) return false;
    char nameBuf[SIGNAL_NAME_MAX + 1] = {0};
    memcpy(nameBuf, &buf[off], nameLen);
    entry.name = String(nameBuf);
    off += nameLen;

    if (entry.kind == SIG_RF) {
        if (off + 68 > len) return false;   // 4+8+24+32
        memcpy(&entry.rf.frequency, &buf[off], 4); off += 4;
        uint16_t fields[4];
        memcpy(fields, &buf[off], 8); off += 8;
        entry.rf.bitCount   = fields[0];
        entry.rf.shortPulse = fields[1];
        entry.rf.longPulse  = fields[2];
        entry.rf.syncPulse  = fields[3];
        memcpy(entry.rf.protocolName, &buf[off], 24); off += 24;
        memcpy(entry.rf.data, &buf[off], 32); off += 32;
        // RAW 尾部块（v3，旧 blob 无此块：长度不足即按固定码处理）
        if (off + 27 <= len) {
            entry.rf.rawFlag = buf[off++];
            memcpy(entry.rf.rawPreset, &buf[off], 24); off += 24;
            uint16_t cnt = 0;
            memcpy(&cnt, &buf[off], 2); off += 2;
            entry.rf.rawCount = cnt;
            if (off + (int)sizeof(entry.rf.rawFileId) <= len) {
                memcpy(&entry.rf.rawFileId, &buf[off], sizeof(entry.rf.rawFileId));
            }
        }
    } else {
        // IR blob: v2带魔数0xA5+版本字节，其余按v1解析（解码详情缺省为零）
        int ver = 1;
        if (off < len && buf[off] == IR_BLOB_MAGIC) {
            if (off + 2 > len) return false;
            ver = buf[off + 1];
            off += 2;
        }

        // v1/v2共用头部（24+1+16+16+4 = 61）
        if (off + 61 > len) return false;
        memcpy(entry.ir.protocolName, &buf[off], 24); off += 24;
        entry.ir.frameCount = buf[off++];
        memcpy(entry.ir.frameLens, &buf[off], 16); off += 16;
        memcpy(entry.ir.frameGapsMs, &buf[off], 16); off += 16;
        memcpy(&entry.ir.totalEdges, &buf[off], 4); off += 4;
        if (entry.ir.totalEdges > IR_MAX_FRAMES * IR_MAX_FRAME_EDGES) {
            return false;   // 数据异常
        }

        // v2解码详情（位宽8+协议8+码值64+地址32+命令32+AC描述64 = 208）
        if (ver >= 2) {
            if (off + 208 > len) return false;
            memcpy(entry.ir.frameBits, &buf[off], 8); off += 8;
            memcpy(entry.ir.frameProto, &buf[off], 8); off += 8;
            memcpy(entry.ir.frameValue, &buf[off], 64); off += 64;
            memcpy(entry.ir.frameAddr, &buf[off], 32); off += 32;
            memcpy(entry.ir.frameCmd, &buf[off], 32); off += 32;
            char acDesc[64];
            memcpy(acDesc, &buf[off], 64); off += 64;
            acDesc[sizeof(acDesc) - 1] = '\0';
            strncpy(entry.ir.acStateDesc, acDesc, sizeof(entry.ir.acStateDesc) - 1);
        }
        // v1数据: frameBits/frameProto/frameValue/frameAddr/frameCmd/acStateDesc
        // 保持构造函数的零值，界面按"无解码详情"显示

        if (entry.ir.totalEdges > 0) {
            uint32_t edgeBytes = entry.ir.totalEdges * 2;
            if (off + (int)edgeBytes > len) return false;
            if (irEdges != nullptr) {
                uint32_t copy = (edgeBytes / 2 > edgeBufCount) ? edgeBufCount * 2 : edgeBytes;
                memcpy(irEdges, &buf[off], copy);
            }
            off += (int)edgeBytes;
        }
    }
    return true;
}

bool DataStore::DecodeSignalBlob(const uint8_t* data, size_t size, SignalEntry& entry) {
    return deserializeSignal(data, size, entry, nullptr, 0);
}

int DataStore::EncodeRfSignalBlob(const SignalEntry& entry, uint8_t* data, size_t size) {
    return entry.kind == SIG_RF ? serializeSignal(entry, nullptr, data, size) : 0;
}

// ==================== 信号存储实现（NVS blob） ====================

// HomeProfile blob 序列化辅助（与信号格式独立）
static int serializeHomeProfile(const HomeProfile& p, uint8_t* buf, int bufSize) {
    int nameLen = p.name.length();
    if (nameLen > HOME_NAME_MAX) nameLen = HOME_NAME_MAX;
    int total = 1 + 1 + nameLen + (int)sizeof(p.slotMap);
    if (buf == nullptr || total > bufSize) return 0;
    int off = 0;
    buf[off++] = p.layout;
    buf[off++] = (uint8_t)nameLen;
    if (nameLen > 0) { memcpy(&buf[off], p.name.c_str(), nameLen); off += nameLen; }
    memcpy(&buf[off], p.slotMap, sizeof(p.slotMap)); off += sizeof(p.slotMap);
    return off;
}
static bool deserializeHomeProfile(const uint8_t* buf, int len, HomeProfile& out) {
    if (buf == nullptr || len < 2 + (int)sizeof(out.slotMap)) return false;
    int off = 0;
    out.layout = buf[off++];
    if (out.layout > HOME_LAYOUT_KEY9) out.layout = HomeLayoutLegacyToNew(out.layout > 8 ? 0 : out.layout);
    int nameLen = buf[off++];
    if (nameLen < 0 || nameLen > HOME_NAME_MAX) return false;
    if (off + nameLen + (int)sizeof(out.slotMap) > len) return false;
    char nb[HOME_NAME_MAX + 1] = {0};
    if (nameLen > 0) memcpy(nb, &buf[off], nameLen);
    out.name = String(nb);
    off += nameLen;
    memcpy(out.slotMap, &buf[off], sizeof(out.slotMap));
    return true;
}
static void homeProfileKey(int id, char* out, size_t sz) {
    snprintf(out, sz, "%s%d", KEY_HPROF_PREFIX, id);
}

bool DataStore::begin() {
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // 统计新域
        int rfCount = 0, irCount = 0, legacyCount = 0;
        preferences.begin(KEY_SIGNAL_NS, true);
        char key[SIGNAL_KEY_LEN];
        for (int i = 1; i <= RF_SLOT_COUNT; i++) {
            signalKeyForKind(SIG_RF, i, key, sizeof(key));
            if (preferences.isKey(key)) rfCount++;
        }
        for (int i = 1; i <= IR_SLOT_COUNT; i++) {
            signalKeyForKind(SIG_IR, i, key, sizeof(key));
            if (preferences.isKey(key)) irCount++;
        }
        for (int i = 1; i <= SIGNAL_SLOT_COUNT; i++) {
            snprintf(key, sizeof(key), "sig_%d", i);
            if (preferences.isKey(key)) legacyCount++;
        }
        preferences.end();
        LOG_I("DataStore: NVS信号存储就绪 RF=%d IR=%d legacy=%d", rfCount, irCount, legacyCount);

        // 旧 sig_N 迁移到 rf_/ir_（首次升级）
        if (legacyCount > 0) {
            int kindMap[SIGNAL_SLOT_COUNT + 1];
            memset(kindMap, -1, sizeof(kindMap));
            preferences.begin(KEY_SIGNAL_NS, true);
            // 先读取所有旧blob到内存并确定kind
            struct LegacyItem { int idx; uint8_t* buf; size_t len; SignalKind kind; };
            std::vector<LegacyItem> items;
            for (int i = 1; i <= SIGNAL_SLOT_COUNT; i++) {
                snprintf(key, sizeof(key), "sig_%d", i);
                if (!preferences.isKey(key)) continue;
                size_t len = preferences.getBytesLength(key);
                if (len == 0 || len >= 32 * 1024) continue;
                uint8_t* buf = (uint8_t*)anyMalloc(len);
                if (!buf) continue;
                if (preferences.getBytes(key, buf, len) != len) { free(buf); continue; }
                SignalEntry e;
                if (!deserializeSignal(buf, (int)len, e, nullptr, 0)) { free(buf); continue; }
                kindMap[i] = (int)e.kind;
                items.push_back({i, buf, len, e.kind});
            }
            preferences.end();
            // 写入新键并删除旧键（需读写模式）
            int migrated = 0;
            preferences.begin(KEY_SIGNAL_NS, false);
            for (auto &it : items) {
                char newKey[SIGNAL_KEY_LEN];
                signalKeyForKind(it.kind, it.idx, newKey, sizeof(newKey));
                // 若新键已存在则跳过（避免覆盖已手动迁移的数据）
                if (!preferences.isKey(newKey)) {
                    if (preferences.putBytes(newKey, it.buf, it.len) == it.len) {
                        migrated++;
                        LOG_I("DataStore: 迁移 sig_%d -> %s (%s)", it.idx, newKey, it.kind==SIG_RF?"RF":"IR");
                    }
                }
                char oldKey[SIGNAL_KEY_LEN];
                snprintf(oldKey, sizeof(oldKey), "sig_%d", it.idx);
                preferences.remove(oldKey);
                free(it.buf);
            }
            preferences.end();
            LOG_I("DataStore: 旧共用槽位迁移完成 %d 条", migrated);
            // 主页 slotMap 编码迁移：旧值 1..100 若对应的是IR信号，则改为 101..200
            if (migrated > 0) {
                preferences.begin(KEY_NAMESPACE, true);
                bool hasAny = false;
                char hk[16];
                for (int i = 0; i < HOME_PROFILE_MAX; i++) { homeProfileKey(i, hk, sizeof(hk)); if (preferences.isKey(hk)) { hasAny = true; break; } }
                preferences.end();
                if (hasAny) {
                    auto homes = ListHomeProfiles();
                    for (auto &h : homes) {
                        bool dirty = false;
                        for (int k = 0; k < 9; k++) {
                            int v = h.slotMap[k];
                            if (v >= 1 && v <= SIGNAL_SLOT_COUNT && kindMap[v] == SIG_IR) {
                                h.slotMap[k] = (int16_t)(v + SLOT_IR_OFFSET);
                                dirty = true;
                            }
                        }
                        if (dirty) SaveHomeProfile(h);
                    }
                }
            }
            // 重新统计
            preferences.begin(KEY_SIGNAL_NS, true);
            rfCount = 0; irCount = 0;
            for (int i = 1; i <= RF_SLOT_COUNT; i++) { signalKeyForKind(SIG_RF, i, key, sizeof(key)); if (preferences.isKey(key)) rfCount++; }
            for (int i = 1; i <= IR_SLOT_COUNT; i++) { signalKeyForKind(SIG_IR, i, key, sizeof(key)); if (preferences.isKey(key)) irCount++; }
            preferences.end();
            LOG_I("DataStore: 迁移后 RF=%d IR=%d", rfCount, irCount);
        }

        // 多主页初始化 / 旧 QuickKey 迁移（首次启动无 HPROF 时执行）
        preferences.begin(KEY_NAMESPACE, true);
        bool hasHome = false;
        char hk[16];
        for (int i = 0; i < HOME_PROFILE_MAX; i++) {
            homeProfileKey(i, hk, sizeof(hk));
            if (preferences.isKey(hk)) { hasHome = true; break; }
        }
        int oldKeys[9] = {0};
        bool hasOldKeys = false;
        if (!hasHome) {
            oldKeys[0] = (int)preferences.getUInt(KEY_QUICKKEY_1, 0);
            oldKeys[1] = (int)preferences.getUInt(KEY_QUICKKEY_2, 0);
            oldKeys[2] = (int)preferences.getUInt(KEY_QUICKKEY_3, 0);
            oldKeys[3] = (int)preferences.getUInt(KEY_QUICKKEY_4, 0);
            oldKeys[4] = (int)preferences.getUInt(KEY_QUICKKEY_5, 0);
            oldKeys[5] = (int)preferences.getUInt(KEY_QUICKKEY_6, 0);
            oldKeys[6] = (int)preferences.getUInt(KEY_QUICKKEY_7, 0);
            oldKeys[7] = (int)preferences.getUInt(KEY_QUICKKEY_8, 0);
            oldKeys[8] = (int)preferences.getUInt(KEY_QUICKKEY_9, 0);
            for (int i = 0; i < 9; i++) if (oldKeys[i] != 0) hasOldKeys = true;
        }
        preferences.end();

        if (!hasHome) {
            preferences.begin(KEY_NAMESPACE, false);
            HomeProfile p0;
            p0.id = 0;
            p0.layout = HOME_LAYOUT_KEY9;
            p0.name = hasOldKeys ? "默认主页" : "主页1";
            for (int i = 0; i < 9; i++) p0.slotMap[i] = (int16_t)oldKeys[i];
            uint8_t buf[1 + 1 + HOME_NAME_MAX + 18];
            int blen = serializeHomeProfile(p0, buf, sizeof(buf));
            homeProfileKey(0, hk, sizeof(hk));
            if (blen > 0) preferences.putBytes(hk, buf, blen);
            preferences.putUChar(KEY_HPROF_ACTIVE, 0);
            preferences.end();
            LOG_I("DataStore: 已初始化默认主页 (迁移旧快捷键=%s)", hasOldKeys ? "是" : "否");
        } else {
            // 旧布局值一次性迁移（旧: 0=九宫格,1=四大键,2=单键,3=空调 → 新: 0=单键..8=九键）
            preferences.begin(KEY_NAMESPACE, true);
            int hver = (int)preferences.getUChar(KEY_HPROF_VER, 0);
            bool hasActive = preferences.isKey(KEY_HPROF_ACTIVE);
            preferences.end();
            if (hver < 2) {
                preferences.begin(KEY_NAMESPACE, false);
                bool anyChanged = false;
                for (int i = 0; i < HOME_PROFILE_MAX; i++) {
                    homeProfileKey(i, hk, sizeof(hk));
                    if (!preferences.isKey(hk)) continue;
                    size_t len = preferences.getBytesLength(hk);
                    if (len < 3) continue;
                    uint8_t* b = (uint8_t*)anyMalloc(len);
                    if (!b) continue;
                    if (preferences.getBytes(hk, b, len) == len) {
                        uint8_t oldLayout = b[0];
                        if (oldLayout <= 3) {
                            b[0] = HomeLayoutLegacyToNew(oldLayout);
                            preferences.putBytes(hk, b, len);
                            anyChanged = true;
                            LOG_I("DataStore: 主页%d 布局迁移 %d -> %d", i, oldLayout, b[0]);
                        }
                    }
                    free(b);
                }
                preferences.putUChar(KEY_HPROF_VER, 2);
                preferences.end();
                LOG_I("DataStore: 布局版本迁移完成 changed=%s", anyChanged ? "是" : "否");
            }
            if (!hasActive) {
                preferences.begin(KEY_NAMESPACE, false);
                preferences.putUChar(KEY_HPROF_ACTIVE, 0);
                preferences.end();
            }
        }

        xSemaphoreGiveRecursive(preferencesMutex);
    }
    return true;
}

bool DataStore::SaveSignal(int index, const SignalEntry& entry, const uint16_t* irEdges) {
    StorageGuard storage(*this);
    if (!storage) return false;
    SignalKind k = entry.kind;
    int limit = (k == SIG_RF) ? RF_SLOT_COUNT : IR_SLOT_COUNT;
    if (index < 1 || index > limit) {
        LOG_E("DataStore::SaveSignal: 无效的槽位索引");
        return false;
    }
    if (entry.kind == SIG_IR && irEdges == nullptr) {
        LOG_E("DataStore::SaveSignal: 红外信号缺少边沿数据");
        return false;
    }
    int bufSize = 2 + SIGNAL_NAME_MAX + 24 + 2 + 61 + 208
                  + IR_MAX_FRAMES * IR_MAX_FRAME_EDGES * 2 + 8;
    uint8_t* buf = (uint8_t*)anyMalloc(bufSize);
    if (buf == nullptr) {
        LOG_E("DataStore::SaveSignal: 缓冲分配失败");
        return false;
    }
    int len = serializeSignal(entry, irEdges, buf, bufSize);
    bool ok = false;
    if (len > 0) {
        if (k == SIG_RF && entry.rf.rawFlag) {
            char path[RawSignalFiles::PathSize];
            auto* fs = RawSignalFiles::locate(index, entry.rf.rawFileId, path);
            if (!fs || !fs->exists(path)) {
                LOG_E("DataStore::SaveSignal: RAW 波形不可用，槽位 %d", index);
                free(buf);
                return false;
            }
        }
        SignalEntry previous;
        if (k == SIG_RF) previous = ReadSignal(SIG_RF, index);
        char key[SIGNAL_KEY_LEN];
        signalKeyForKind(k, index, key, sizeof(key));
        preferences.begin(KEY_SIGNAL_NS, false);
        ok = preferences.putBytes(key, buf, len) == (size_t)len;
        preferences.end();
        if (ok) ++remoteConfigRevision;
        if (ok && previous.rf.rawFlag && (!entry.rf.rawFlag || previous.rf.rawFileId != entry.rf.rawFileId)) {
            RawSignalFiles::remove(index, previous.rf.rawFileId);
        }
    } else {
        LOG_E("DataStore::SaveSignal: 序列化失败");
    }
    free(buf);
    LOG_I("DataStore::SaveSignal: %s_%d kind=%s len=%d -> %s",
          k==SIG_RF?"rf":"ir", index, k == SIG_RF ? "RF" : "IR", len, ok ? "OK" : "FAIL");
    return ok;
}

bool DataStore::RenameIRSignal(int index, const String& name) {
    if (index < 1 || index > IR_SLOT_COUNT || name.isEmpty()) return false;
    StorageGuard storage(*this);
    if (!storage) return false;

    char key[SIGNAL_KEY_LEN];
    signalKeyForKind(SIG_IR, index, key, sizeof(key));
    if (!preferences.begin(KEY_SIGNAL_NS, false)) return false;
    const size_t len = preferences.getBytesLength(key);
    const size_t nameLen = std::min<size_t>(name.length(), SIGNAL_NAME_MAX);
    auto* buf = len >= 3 && len < 32 * 1024
        ? static_cast<uint8_t*>(anyMalloc(len + nameLen)) : nullptr;
    SignalEntry entry;
    bool ok = buf && preferences.getBytes(key, buf, len) == len && buf[0] == SIG_IR &&
              DecodeSignalBlob(buf, len, entry);
    if (ok) {
        // Keep the stored metadata and every pulse byte unchanged.
        const size_t oldNameLen = buf[1];
        const size_t payload = len - 2 - oldNameLen;
        memmove(buf + 2 + nameLen, buf + 2 + oldNameLen, payload);
        memcpy(buf + 2, name.c_str(), nameLen);
        buf[1] = nameLen;
        const size_t newSize = 2 + nameLen + payload;
        ok = preferences.putBytes(key, buf, newSize) == newSize;
        if (ok) ++remoteConfigRevision;
    }
    free(buf);
    preferences.end();
    return ok;
}

SignalEntry DataStore::ReadSignal(SignalKind kind, int index) {
    SignalEntry entry;
    int limit = (kind == SIG_RF) ? RF_SLOT_COUNT : IR_SLOT_COUNT;
    if (index < 1 || index > limit) return entry;
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        char key[SIGNAL_KEY_LEN];
        signalKeyForKind(kind, index, key, sizeof(key));
        preferences.begin(KEY_SIGNAL_NS, true);
        if (preferences.isKey(key)) {
            size_t len = preferences.getBytesLength(key);
            if (len > 0 && len < 32 * 1024) {
                uint8_t* buf = (uint8_t*)anyMalloc(len);
                if (buf != nullptr) {
                    if (preferences.getBytes(key, buf, len) == len) {
                        if (!deserializeSignal(buf, (int)len, entry, nullptr, 0)) {
                            entry = SignalEntry();
                        } else if (entry.kind != kind) {
                            // 类型不匹配视为损坏（旧共用数据残留不应出现）
                            entry = SignalEntry();
                        } else if (kind == SIG_RF) {
                            entry.rf.rawSlot = (uint8_t)index;  // 运行期：回放入口
                        }
                    }
                    free(buf);
                }
            }
        }
        preferences.end();
        xSemaphoreGiveRecursive(preferencesMutex);
    }
    return entry;
}

SignalEntry DataStore::ReadSignal(int index) {
    // 兼容：先RF再IR
    if (index >= 1 && index <= RF_SLOT_COUNT) {
        SignalEntry e = ReadSignal(SIG_RF, index);
        if (e.name.length() > 0) return e;
    }
    if (index >= 1 && index <= IR_SLOT_COUNT) {
        SignalEntry e = ReadSignal(SIG_IR, index);
        if (e.name.length() > 0) return e;
    }
    return SignalEntry();
}

bool DataStore::SignalExists(SignalKind kind, int index) {
    int limit = (kind == SIG_RF) ? RF_SLOT_COUNT : IR_SLOT_COUNT;
    if (index < 1 || index > limit) return false;
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        char key[SIGNAL_KEY_LEN];
        signalKeyForKind(kind, index, key, sizeof(key));
        preferences.begin(KEY_SIGNAL_NS, true);
        bool exists = preferences.isKey(key);
        preferences.end();
        xSemaphoreGiveRecursive(preferencesMutex);
        return exists;
    }
    return false;
}

bool DataStore::SignalExists(int index) {
    return SignalExists(SIG_RF, index) || SignalExists(SIG_IR, index);
}

bool DataStore::DeleteSignal(SignalKind kind, int index) {
    StorageGuard storage(*this);
    if (!storage) return false;
    int limit = (kind == SIG_RF) ? RF_SLOT_COUNT : IR_SLOT_COUNT;
    if (index < 1 || index > limit) return false;
    const SignalEntry previous = kind == SIG_RF ? ReadSignal(kind, index) : SignalEntry();
    char key[SIGNAL_KEY_LEN];
    signalKeyForKind(kind, index, key, sizeof(key));
    preferences.begin(KEY_SIGNAL_NS, false);
    bool ok = preferences.remove(key);
    preferences.end();
    if (ok) ++remoteConfigRevision;
    LOG_I("DataStore::DeleteSignal: %s_%d -> %s", kind==SIG_RF?"rf":"ir", index, ok ? "OK" : "FAIL");
    if (ok) {
        // RF 槽位：同步删除 RAW 边车文件
        if (previous.rf.rawFlag) RawSignalFiles::remove(index, previous.rf.rawFileId);
        int slot = KindIndexToSlot(kind, index);
        auto homes = ListHomeProfiles();
        for (auto& h : homes) {
            bool dirty = false;
            for (int i = 0; i < 9; i++) if (h.slotMap[i] == slot) { h.slotMap[i] = 0; dirty = true; }
            if (dirty) SaveHomeProfile(h);
        }
    }
    return ok;
}

bool DataStore::DeleteSignal(int index) {
    bool a = false, b = false;
    if (index >= 1 && index <= RF_SLOT_COUNT) a = DeleteSignal(SIG_RF, index);
    if (index >= 1 && index <= IR_SLOT_COUNT) b = DeleteSignal(SIG_IR, index);
    // 清理旧编码下 slotMap 仍为统一索引的情况
    if (!a && !b) {
        auto homes = ListHomeProfiles();
        for (auto& h : homes) {
            bool dirty = false;
            for (int i = 0; i < 9; i++) if (h.slotMap[i] == index) { h.slotMap[i] = 0; dirty = true; }
            if (dirty) SaveHomeProfile(h);
        }
    }
    return a || b;
}

bool DataStore::ReadIREdges(int index, uint16_t* buffer, uint32_t bufferCount, IRSignalMeta* meta) {
    if (index < 1 || index > IR_SLOT_COUNT || buffer == nullptr) return false;
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        char key[SIGNAL_KEY_LEN];
        signalKeyForKind(SIG_IR, index, key, sizeof(key));
        bool ok = false;
        preferences.begin(KEY_SIGNAL_NS, true);
        if (preferences.isKey(key)) {
            size_t len = preferences.getBytesLength(key);
            uint8_t* buf = (uint8_t*)anyMalloc(len);
            if (buf != nullptr) {
                if (preferences.getBytes(key, buf, len) == len) {
                    SignalEntry entry;
                    if (deserializeSignal(buf, (int)len, entry, buffer, bufferCount)) {
                        ok = entry.kind == SIG_IR && entry.ir.totalEdges <= bufferCount;
                        if (ok && meta) *meta = entry.ir;
                    }
                }
                free(buf);
            }
        }
        preferences.end();
        xSemaphoreGiveRecursive(preferencesMutex);
        return ok;
    }
    return false;
}

int DataStore::GetSignalCount(SignalKind kind) {
    int count = 0;
    int limit = (kind == SIG_RF) ? RF_SLOT_COUNT : IR_SLOT_COUNT;
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        preferences.begin(KEY_SIGNAL_NS, true);
        char key[SIGNAL_KEY_LEN];
        for (int i = 1; i <= limit; i++) {
            signalKeyForKind(kind, i, key, sizeof(key));
            if (preferences.isKey(key)) count++;
        }
        preferences.end();
        xSemaphoreGiveRecursive(preferencesMutex);
    }
    return count;
}

int DataStore::GetSignalCount() {
    return GetSignalCount(SIG_RF) + GetSignalCount(SIG_IR);
}

void DataStore::ClearAllSignals(SignalKind kind) {
    StorageGuard storage(*this);
    if (!storage) return;
    char key[SIGNAL_KEY_LEN];
    int limit = (kind == SIG_RF) ? RF_SLOT_COUNT : IR_SLOT_COUNT;
    for (int i = 1; i <= limit; i++) {
        const SignalEntry previous = kind == SIG_RF ? ReadSignal(kind, i) : SignalEntry();
        signalKeyForKind(kind, i, key, sizeof(key));
        preferences.begin(KEY_SIGNAL_NS, false);
        bool removed = preferences.remove(key);
        preferences.end();
        if (removed && previous.rf.rawFlag) RawSignalFiles::remove(i, previous.rf.rawFileId);
    }
    ++remoteConfigRevision;
    LOG_I("DataStore: 已清空 %s 信号", kind==SIG_RF?"RF":"IR");
    // 清理主页绑定
    auto homes = ListHomeProfiles();
    for (auto& h : homes) {
        bool dirty = false;
        for (int i = 0; i < 9; i++) {
            int v = h.slotMap[i];
            if (v == 0) continue;
            if (kind == SIG_RF && SlotIsRF(v)) { /* RF槽被清空，RF域的绑定清零 */ }
            // 需判断该slot是否属于被清空的域
            bool belongs = (kind == SIG_RF) ? SlotIsRF(v) : SlotIsIR(v);
            // 旧编码下1..100全视为RF，需一并清理
            if (kind == SIG_RF && v >= 1 && v <= RF_SLOT_COUNT) belongs = true;
            if (belongs) { h.slotMap[i] = 0; dirty = true; }
        }
        if (dirty) SaveHomeProfile(h);
    }
}

void DataStore::ClearAllSignals() {
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        preferences.begin(KEY_SIGNAL_NS, false);
        preferences.clear();
        preferences.end();
        ++remoteConfigRevision;
        xSemaphoreGiveRecursive(preferencesMutex);
        LOG_I("DataStore: 已清空全部信号数据");
    }
}

// ==================== 快捷键（NVS） ====================

void DataStore::SaveQuickKey(QuickKey keyData)
{
    if (preferencesMutex == nullptr) {
        LOG_E("DataStore::SaveQuickKey: 互斥锁未初始化");
        return;
    }
    
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        preferences.begin(KEY_NAMESPACE);

        preferences.putUInt(KEY_QUICKKEY_1, keyData.key1);
        preferences.putUInt(KEY_QUICKKEY_2, keyData.key2);
        preferences.putUInt(KEY_QUICKKEY_3, keyData.key3);
        preferences.putUInt(KEY_QUICKKEY_4, keyData.key4);
        preferences.putUInt(KEY_QUICKKEY_5, keyData.key5);
        preferences.putUInt(KEY_QUICKKEY_6, keyData.key6);
        preferences.putUInt(KEY_QUICKKEY_7, keyData.key7);
        preferences.putUInt(KEY_QUICKKEY_8, keyData.key8);
        preferences.putUInt(KEY_QUICKKEY_9, keyData.key9);
        
        preferences.end();
        
        xSemaphoreGiveRecursive(preferencesMutex);
    } else {
        LOG_W("DataStore::SaveQuickKey: 获取互斥锁超时");
    }
}

QuickKey DataStore::LoadQuickKey()
{
    QuickKey quickKey = {0}; // 初始化为0
    
    if (preferencesMutex == nullptr) {
        LOG_E("DataStore::LoadQuickKey: 互斥锁未初始化");
        return quickKey;
    }
    
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        preferences.begin(KEY_NAMESPACE);

        quickKey.key1 = preferences.getUInt(KEY_QUICKKEY_1, 0);
        quickKey.key2 = preferences.getUInt(KEY_QUICKKEY_2, 0);
        quickKey.key3 = preferences.getUInt(KEY_QUICKKEY_3, 0);
        quickKey.key4 = preferences.getUInt(KEY_QUICKKEY_4, 0);
        quickKey.key5 = preferences.getUInt(KEY_QUICKKEY_5, 0);
        quickKey.key6 = preferences.getUInt(KEY_QUICKKEY_6, 0);
        quickKey.key7 = preferences.getUInt(KEY_QUICKKEY_7, 0);
        quickKey.key8 = preferences.getUInt(KEY_QUICKKEY_8, 0);
        quickKey.key9 = preferences.getUInt(KEY_QUICKKEY_9, 0);
        preferences.end();

        xSemaphoreGiveRecursive(preferencesMutex);
    } else {
        LOG_W("DataStore::LoadQuickKey: 获取互斥锁超时");
    }
    
    return quickKey;
}

// ==================== 系统配置（NVS） ====================

void DataStore::SaveSystemConfig(SystemConfig systemConfig)
{
    if (preferencesMutex == nullptr) {
        LOG_E("DataStore::SaveSystemConfig: 互斥锁未初始化");
        return;
    }
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        preferences.begin(KEY_NAMESPACE);

        preferences.putBool(KEY_SOUND_ALERT_ENABLE, systemConfig.soundAlertEnable);
        preferences.putInt(KEY_REPEAT_TRANSMIT, systemConfig.repeatTransmit);
        preferences.putInt(KEY_TX_POWER_LEVEL, systemConfig.txPowerLevel);
        preferences.putInt(KEY_RX_SENSITIVITY, systemConfig.rxSensitivity);
        preferences.putInt(KEY_BRIGHTNESS, systemConfig.brightness);
        preferences.putLong(KEY_AUTO_SLEEP_TIME, systemConfig.autoSleepTime);
        preferences.putLong(KEY_AUTO_SCREEN_OFF_TIME, systemConfig.autoScreenOffTime);
        preferences.putBool(KEY_AP_ENABLED, systemConfig.APEnabled);
        preferences.putBool(KEY_WIFI_ENABLED, systemConfig.WifiEnabled);
        preferences.putString(KEY_AP_NAME, systemConfig.APName);
        preferences.putString(KEY_AP_PASSWORD, systemConfig.APPassword);
        preferences.putString(KEY_WIFI_NAME, systemConfig.WifiName);
        preferences.putString(KEY_WIFI_PASSWORD, systemConfig.WifiPassword);
        preferences.putInt(KEY_BOOT_ANIM, systemConfig.bootAnim);
        preferences.end();

        xSemaphoreGiveRecursive(preferencesMutex);
    } else {
        LOG_W("DataStore::SaveSystemConfig: 获取互斥锁超时");
    }
    LOG_I("DataStore: SaveSystemConfig() 执行完成");
}

SystemConfig DataStore::LoadSystemConfig()
{
    SystemConfig systemConfig = {0}; // 初始化为0
    
    if (preferencesMutex == nullptr) {
        LOG_E("DataStore::LoadSystemConfig: 互斥锁未初始化");
        return systemConfig;
    }
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        preferences.begin(KEY_NAMESPACE);

        systemConfig.soundAlertEnable = preferences.getBool(KEY_SOUND_ALERT_ENABLE, true);
        systemConfig.repeatTransmit = preferences.getInt(KEY_REPEAT_TRANSMIT, 15);
        systemConfig.txPowerLevel = preferences.getInt(KEY_TX_POWER_LEVEL, 7);
        systemConfig.rxSensitivity = preferences.getInt(KEY_RX_SENSITIVITY, 1);
        systemConfig.brightness = preferences.getInt(KEY_BRIGHTNESS, 100);
        systemConfig.autoSleepTime = preferences.getLong(KEY_AUTO_SLEEP_TIME, 0);
        systemConfig.autoScreenOffTime = preferences.getLong(KEY_AUTO_SCREEN_OFF_TIME, 0);
        systemConfig.APEnabled = preferences.getBool(KEY_AP_ENABLED, false);
        systemConfig.WifiEnabled = preferences.getBool(KEY_WIFI_ENABLED, false);
        systemConfig.APName = preferences.getString(KEY_AP_NAME, "MYNOVA_RFC");
        systemConfig.APPassword = preferences.getString(KEY_AP_PASSWORD, "MYNOVA123");
        systemConfig.WifiName = preferences.getString(KEY_WIFI_NAME, "");
        systemConfig.WifiPassword = preferences.getString(KEY_WIFI_PASSWORD, "");
        systemConfig.bootAnim = preferences.getInt(KEY_BOOT_ANIM, 1);
        preferences.end();

        xSemaphoreGiveRecursive(preferencesMutex);
    } else {
        LOG_W("DataStore::LoadSystemConfig: 获取互斥锁超时");
    }
    
    return systemConfig;
}

void DataStore::ClearAllData()
{
    StorageGuard storage(*this, portMAX_DELAY);
    if (!storage) {
        LOG_W("DataStore::ClearAllData: 无法获取存储锁");
        return;
    }

    LOG_I("DataStore: ClearAllData() 开始清空所有数据");
    preferences.begin(KEY_NAMESPACE);
    preferences.clear();
    preferences.end();

    ClearAllSignals();
    ClearMQTTConfig();
    if (preferences.begin(Log::NVS_NS, false)) {
        if (!preferences.clear()) LOG_E("DataStore: 清除日志配置失败");
        preferences.end();
    } else {
        LOG_E("DataStore: 无法打开日志配置");
    }
    Log::reloadLevel();
    if (RawCaptureSession::ensureMounted() && LittleFS.exists(RAW_TEMP_FILE)) {
        LittleFS.remove(RAW_TEMP_FILE);
    }
    LOG_I("DataStore: ClearAllData() 执行完成");
}

// ==================== WiFi配置（NVS） ====================

bool DataStore::SaveWiFiConfig(const String& ssid, const String& password)
{
    if (preferencesMutex == nullptr) {
        LOG_E("DataStore::SaveWiFiConfig: 互斥锁未初始化");
        return false;
    }
    
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        bool success = preferences.begin(KEY_NAMESPACE, false);
        
        if (!success) {
            LOG_E("DataStore::SaveWiFiConfig: 打开Preferences失败");
            xSemaphoreGiveRecursive(preferencesMutex);
            return false;
        }
        
        preferences.putString(KEY_WIFI_NAME, ssid);
        preferences.putString(KEY_WIFI_PASSWORD, password);
        preferences.end();
        
        xSemaphoreGiveRecursive(preferencesMutex);
        
        LOG_I("DataStore: WiFi配置已保存");
        return true;
    } else {
        LOG_W("DataStore::SaveWiFiConfig: 获取互斥锁超时");
        return false;
    }
}

bool DataStore::LoadWiFiConfig(String& ssid, String& password)
{
    if (preferencesMutex == nullptr) {
        LOG_E("DataStore::LoadWiFiConfig: 互斥锁未初始化");
        return false;
    }
    
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        bool success = preferences.begin(KEY_NAMESPACE, true);
        
        if (!success) {
            LOG_E("DataStore::LoadWiFiConfig: 打开Preferences失败");
            xSemaphoreGiveRecursive(preferencesMutex);
            return false;
        }
        
        ssid = preferences.getString(KEY_WIFI_NAME, "");
        password = preferences.getString(KEY_WIFI_PASSWORD, "");
        preferences.end();
        
        xSemaphoreGiveRecursive(preferencesMutex);
        
        if (ssid.length() > 0) {
            LOG_I("DataStore: WiFi配置已加载");
            return true;
        }
        
        return false;
    } else {
        LOG_W("DataStore::LoadWiFiConfig: 获取互斥锁超时");
        return false;
    }
}

void DataStore::ClearWiFiConfig()
{
    if (preferencesMutex == nullptr) {
        LOG_E("DataStore::ClearWiFiConfig: 互斥锁未初始化");
        return;
    }
    
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        bool success = preferences.begin(KEY_NAMESPACE, false);
        
        if (!success) {
            LOG_E("DataStore::ClearWiFiConfig: 打开Preferences失败");
            xSemaphoreGiveRecursive(preferencesMutex);
            return;
        }
        
        preferences.putString(KEY_WIFI_NAME, "");
        preferences.putString(KEY_WIFI_PASSWORD, "");
        preferences.end();
        
        xSemaphoreGiveRecursive(preferencesMutex);
        
        LOG_I("DataStore: WiFi配置已清除");
    } else {
        LOG_W("DataStore::ClearWiFiConfig: 获取互斥锁超时");
    }
}

// ==================== MQTT配置（NVS） ====================

bool DataStore::SaveMQTTConfig(MQTTMode mode, const String& server, uint16_t port, const String& username, const String& password)
{
    if (preferencesMutex == nullptr) {
        LOG_E("DataStore::SaveMQTTConfig: 互斥锁未初始化");
        return false;
    }
    
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        bool success = preferences.begin(MQTT_NAMESPACE, false);
        
        if (!success) {
            LOG_E("DataStore::SaveMQTTConfig: 打开Preferences失败");
            xSemaphoreGiveRecursive(preferencesMutex);
            return false;
        }
        
        String modeStr = mqttModeToString(mode);
        preferences.putString(MQTT_KEY_MODE, modeStr);
        preferences.putString(MQTT_KEY_SERVER, server);
        preferences.putUShort(MQTT_KEY_PORT, port);
        preferences.putString(MQTT_KEY_USERNAME, username);
        preferences.putString(MQTT_KEY_PASSWORD, password);
        preferences.end();
        
        xSemaphoreGiveRecursive(preferencesMutex);
        
        LOG_I("DataStore: MQTT配置已保存");
        LOG_I("  模式: %s", modeStr.c_str());
        if (mode == MQTTMode::MANUAL && server.length() > 0) {
            LOG_I("  服务器: %s:%d", server.c_str(), port);
        }
        if (username.length() > 0) {
            LOG_I("  用户名: %s", username.c_str());
        }
        return true;
    } else {
        LOG_W("DataStore::SaveMQTTConfig: 获取互斥锁超时");
        return false;
    }
}

bool DataStore::LoadMQTTConfig(MQTTMode& mode, String& server, uint16_t& port, String& username, String& password)
{
    if (preferencesMutex == nullptr) {
        LOG_E("DataStore::LoadMQTTConfig: 互斥锁未初始化");
        return false;
    }
    
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        bool success = preferences.begin(MQTT_NAMESPACE, true);
        
        if (!success) {
            // 只读打开失败通常意味着"mqtt"命名空间从未创建过（设备从未配置过MQTT），
            // 属于正常状态而非错误：静默返回"无配置"，避免loop轮询场景下刷屏日志
            xSemaphoreGiveRecursive(preferencesMutex);
            return false;
        }
        
        String modeStr = preferences.getString(MQTT_KEY_MODE, "");
        mode = stringToMQTTMode(modeStr);
        server = preferences.getString(MQTT_KEY_SERVER, "");
        port = preferences.getUShort(MQTT_KEY_PORT, 1883);
        username = preferences.getString(MQTT_KEY_USERNAME, "");
        password = preferences.getString(MQTT_KEY_PASSWORD, "");
        preferences.end();
        
        xSemaphoreGiveRecursive(preferencesMutex);
        
        if (modeStr.length() > 0) {
            LOG_I("DataStore: MQTT配置已加载");
            LOG_I("  模式: %s", modeStr.c_str());
            if (mode == MQTTMode::MANUAL && server.length() > 0) {
                LOG_I("  服务器: %s:%d", server.c_str(), port);
            }
            if (username.length() > 0) {
                LOG_I("  用户名: %s", username.c_str());
            }
            return true;
        }
        
        return false;
    } else {
        LOG_W("DataStore::LoadMQTTConfig: 获取互斥锁超时");
        return false;
    }
}

void DataStore::ClearMQTTConfig()
{
    if (preferencesMutex == nullptr) {
        LOG_E("DataStore::ClearMQTTConfig: 互斥锁未初始化");
        return;
    }
    
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        bool success = preferences.begin(MQTT_NAMESPACE, false);
        
        if (!success) {
            LOG_E("DataStore::ClearMQTTConfig: 打开Preferences失败");
            xSemaphoreGiveRecursive(preferencesMutex);
            return;
        }
        
        preferences.clear();
        preferences.end();
        
        xSemaphoreGiveRecursive(preferencesMutex);
        
        LOG_I("DataStore: MQTT配置已清除");
    } else {
        LOG_W("DataStore::ClearMQTTConfig: 获取互斥锁超时");
    }
}

// ==================== 多主页配置（NVS，与系统配置同命名空间 RadioData） ====================
bool DataStore::LoadHomeProfile(int id, HomeProfile& out) {
    if (id < 0 || id >= HOME_PROFILE_MAX) return false;
    if (preferencesMutex == nullptr) return false;
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    preferences.begin(KEY_NAMESPACE, true);
    char key[16]; homeProfileKey(id, key, sizeof(key));
    bool ok = false;
    if (preferences.isKey(key)) {
        size_t len = preferences.getBytesLength(key);
        uint8_t* buf = (uint8_t*)anyMalloc(len > 0 ? len : 1);
        if (buf) {
            if (len > 0 && preferences.getBytes(key, buf, len) == len) {
                ok = deserializeHomeProfile(buf, (int)len, out);
                if (ok) out.id = (uint8_t)id;
            }
            free(buf);
        }
    }
    preferences.end();
    xSemaphoreGiveRecursive(preferencesMutex);
    return ok;
}

bool DataStore::SaveHomeProfile(const HomeProfile& profile) {
    if (profile.id >= HOME_PROFILE_MAX) return false;
    if (preferencesMutex == nullptr) return false;
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    uint8_t buf[1 + 1 + HOME_NAME_MAX + 18];
    int blen = serializeHomeProfile(profile, buf, sizeof(buf));
    bool ok = false;
    if (blen > 0) {
        preferences.begin(KEY_NAMESPACE, false);
        char key[16]; homeProfileKey(profile.id, key, sizeof(key));
        ok = preferences.putBytes(key, buf, blen) == (size_t)blen;
        preferences.end();
    }
    if (ok) ++remoteConfigRevision;
    xSemaphoreGiveRecursive(preferencesMutex);
    LOG_I("DataStore::SaveHomeProfile id=%d layout=%d name=%s -> %s",
          profile.id, profile.layout, profile.name.c_str(), ok ? "OK" : "FAIL");
    return ok;
}

bool DataStore::DeleteHomeProfile(int id) {
    if (id < 0 || id >= HOME_PROFILE_MAX) return false;
    if (preferencesMutex == nullptr) return false;
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    preferences.begin(KEY_NAMESPACE, false);
    char key[16]; homeProfileKey(id, key, sizeof(key));
    bool ok = preferences.remove(key);
    // 若被删的是 active，补到第一个剩余主页
    if (preferences.isKey(KEY_HPROF_ACTIVE) && ok) {
        int active = (int)preferences.getUChar(KEY_HPROF_ACTIVE, 0);
        if (active == id) {
            int fallback = -1;
            for (int i = 0; i < HOME_PROFILE_MAX; i++) {
                char k[16]; homeProfileKey(i, k, sizeof(k));
                if (preferences.isKey(k)) { fallback = i; break; }
            }
            if (fallback < 0) {
                // 最后一个被删：重建单主页（恢复默认，避免零主页状态）
                HomeProfile def; def.id = 0; def.layout = HOME_LAYOUT_GRID9; def.name = "主页1";
                uint8_t b[1 + 1 + HOME_NAME_MAX + 18]; int bl = serializeHomeProfile(def, b, sizeof(b));
                homeProfileKey(0, key, sizeof(key));
                if (bl > 0) preferences.putBytes(key, b, bl);
                preferences.putUChar(KEY_HPROF_ACTIVE, 0);
                LOG_I("DataStore::DeleteHomeProfile: 已重建默认主页 (末个删除)");
            } else {
                preferences.putUChar(KEY_HPROF_ACTIVE, (uint8_t)fallback);
                LOG_I("DataStore::DeleteHomeProfile: active -> %d", fallback);
            }
        }
    }
    preferences.end();
    if (ok) {
        ++remoteConfigRevision;
        ++activeHomeRevision;
    }
    xSemaphoreGiveRecursive(preferencesMutex);
    LOG_I("DataStore::DeleteHomeProfile id=%d -> %s", id, ok ? "OK" : "FAIL");
    return ok;
}

int DataStore::CreateHomeProfile(const String& name, uint8_t layout) {
    if (preferencesMutex == nullptr) return -1;
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return -1;
    preferences.begin(KEY_NAMESPACE, false);
    int freeId = -1;
    char key[16];
    for (int i = 0; i < HOME_PROFILE_MAX; i++) {
        homeProfileKey(i, key, sizeof(key));
        if (!preferences.isKey(key)) { freeId = i; break; }
    }
    if (freeId < 0) {
        preferences.end(); xSemaphoreGiveRecursive(preferencesMutex); return -1;
    }
    HomeProfile p; p.id = (uint8_t)freeId; p.layout = layout;
    String n = name; if (n.length() > HOME_NAME_MAX) n = n.substring(0, HOME_NAME_MAX);
    if (n.length() == 0) n = "主页" + String(freeId + 1);
    p.name = n;
    memset(p.slotMap, 0, sizeof(p.slotMap));
    uint8_t buf[1 + 1 + HOME_NAME_MAX + 18];
    int blen = serializeHomeProfile(p, buf, sizeof(buf));
    int out = -1;
    if (blen > 0) {
        homeProfileKey(freeId, key, sizeof(key));
        out = (preferences.putBytes(key, buf, blen) == (size_t)blen) ? freeId : -1;
    }
    preferences.end();
    if (out >= 0) ++remoteConfigRevision;
    xSemaphoreGiveRecursive(preferencesMutex);
    LOG_I("DataStore::CreateHomeProfile id=%d layout=%d name=%s -> %s",
          freeId, layout, p.name.c_str(), out >= 0 ? "OK" : "FAIL");
    return out;
}

int DataStore::GetHomeProfileCount() {
    if (preferencesMutex == nullptr) return 0;
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return 0;
    preferences.begin(KEY_NAMESPACE, true);
    int n = 0;
    char key[16];
    for (int i = 0; i < HOME_PROFILE_MAX; i++) {
        homeProfileKey(i, key, sizeof(key));
        if (preferences.isKey(key)) n++;
    }
    preferences.end();
    xSemaphoreGiveRecursive(preferencesMutex);
    return n;
}

std::vector<HomeProfile> DataStore::ListHomeProfiles() {
    std::vector<HomeProfile> out;
    if (preferencesMutex == nullptr) return out;
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return out;
    preferences.begin(KEY_NAMESPACE, true);
    for (int i = 0; i < HOME_PROFILE_MAX; i++) {
        char key[16]; homeProfileKey(i, key, sizeof(key));
        if (!preferences.isKey(key)) continue;
        size_t len = preferences.getBytesLength(key);
        uint8_t* buf = (uint8_t*)anyMalloc(len > 0 ? len : 1);
        if (!buf) continue;
        if (len > 0 && preferences.getBytes(key, buf, len) == len) {
            HomeProfile p; p.id = (uint8_t)i;
            if (deserializeHomeProfile(buf, (int)len, p)) { p.id = (uint8_t)i; out.push_back(p); }
        }
        free(buf);
    }
    preferences.end();
    xSemaphoreGiveRecursive(preferencesMutex);
    return out;
}

int DataStore::GetActiveHomeProfile() {
    if (preferencesMutex == nullptr) return 0;
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return 0;
    preferences.begin(KEY_NAMESPACE, true);
    int active = (int)preferences.getUChar(KEY_HPROF_ACTIVE, 0);
    // 若 active 所指已不存在，回落到首个主页
    char key[16]; homeProfileKey(active, key, sizeof(key));
    if (!preferences.isKey(key)) {
        for (int i = 0; i < HOME_PROFILE_MAX; i++) {
            homeProfileKey(i, key, sizeof(key));
            if (preferences.isKey(key)) { active = i; break; }
        }
    }
    preferences.end();
    xSemaphoreGiveRecursive(preferencesMutex);
    return active;
}

bool DataStore::SetActiveHomeProfile(int id) {
    if (id < 0 || id >= HOME_PROFILE_MAX || preferencesMutex == nullptr) return false;
    if (xSemaphoreTakeRecursive(preferencesMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    preferences.begin(KEY_NAMESPACE, false);
    char key[16]; homeProfileKey(id, key, sizeof(key));
    bool ok = preferences.isKey(key) && preferences.putUChar(KEY_HPROF_ACTIVE, (uint8_t)id) == 1;
    preferences.end();
    if (ok) ++activeHomeRevision;
    xSemaphoreGiveRecursive(preferencesMutex);
    if (ok) LOG_I("DataStore::SetActiveHomeProfile -> %d", id);
    return ok;
}

bool DataStore::HasMQTTConfig()
{
    MQTTMode mode;
    String server;
    uint16_t port;
    String username;
    String password;

    return LoadMQTTConfig(mode, server, port, username, password);
}
