/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef DATASTORE_H
#define DATASTORE_H

#include "../Proto/SignalData.h"
#include <WString.h>
#include <atomic>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum class MQTTMode {
    AUTO,
    MANUAL
};

struct QuickKey{
    int key1;
    int key2;
    int key3;
    int key4;
    int key5;
    int key6;
    int key7;
    int key8;
    int key9;
};

// ==================== 多主页（多桌面）配置 ====================
// 布局 = 按键数量：1..9 键。值即键数-1（HOME_LAYOUT_N(n) = n-1）。
// 按键按行优先顺序铺满可视区：物理键1..N 依序对应视觉顺序的第1..N个按钮。
//   N=1: 全屏单键
//   N=2: 左右两半
//   N=3: 上1大+下2（1键在上半屏，2/3在下半屏左右）
//   N=4: 2x2
//   N=5: 上1通栏 + 下2x2
//   N=6: 2x3
//   N=7: 上1通栏 + 下2x3
//   N=8: 2x4
//   N=9: 3x3
enum HomeLayoutType : uint8_t {
    HOME_LAYOUT_KEY1 = 0,  // 1键（原 SINGLE 单键/升降门）
    HOME_LAYOUT_KEY2 = 1,  // 2键（新增）
    HOME_LAYOUT_KEY3 = 2,  // 3键（新增）
    HOME_LAYOUT_KEY4 = 3,  // 4键（原 GRID4 四大键）
    HOME_LAYOUT_KEY5 = 4,  // 5键（原 AC_REMOTE 空调遥控）
    HOME_LAYOUT_KEY6 = 5,  // 6键（新增）
    HOME_LAYOUT_KEY7 = 6,  // 7键（新增）
    HOME_LAYOUT_KEY8 = 7,  // 8键（新增）
    HOME_LAYOUT_KEY9 = 8   // 9键（原 GRID9 九宫格）
};
// 兼容旧名
#define HOME_LAYOUT_SINGLE     HOME_LAYOUT_KEY1
#define HOME_LAYOUT_GRID4      HOME_LAYOUT_KEY4
#define HOME_LAYOUT_AC_REMOTE  HOME_LAYOUT_KEY5
#define HOME_LAYOUT_GRID9      HOME_LAYOUT_KEY9
// n(1..9) → 布局值
inline uint8_t HomeLayoutFromKeyCount(int n) { return (uint8_t)((n < 1) ? 1 : (n > 9 ? 9 : n)) - 1; }
inline int HomeLayoutToKeyCount(uint8_t layout) { return (int)layout + 1; }
// 旧固件布局值 → 新值（开发阶段一次性迁移用；仅 LoadHomeProfile 内部调用）
inline uint8_t HomeLayoutLegacyToNew(uint8_t old) {
    switch (old) {
        case 0: return HOME_LAYOUT_KEY9;  // 旧 GRID9
        case 1: return HOME_LAYOUT_KEY4;  // 旧 GRID4
        case 2: return HOME_LAYOUT_KEY1;  // 旧 SINGLE
        case 3: return HOME_LAYOUT_KEY5;  // 旧 AC_REMOTE
        default: return HOME_LAYOUT_KEY9;
    }
}

#define HOME_PROFILE_MAX  8   // 最多 8 个主页
#define HOME_NAME_MAX     32  // 主页名称最大字节数（UTF-8，约 10 个汉字）

struct HomeProfile {
    uint8_t id;               // 0..7
    String  name;             // 主页名称（T9 输入）
    uint8_t layout;           // HomeLayoutType（1..9 键布局，值为键数-1）
    int16_t slotMap[9];       // 物理键 1-9 → 信号槽位（0=空，1..100=RF，101..200=IR），未用键恒为 0

    HomeProfile() : id(0), layout(HOME_LAYOUT_KEY9) {
        memset(slotMap, 0, sizeof(slotMap));
    }
};

struct SystemConfig{
    bool soundAlertEnable;      // 声音提示(WM8960滴声)开关
    int repeatTransmit;
    int txPowerLevel;           // CC1101 发射功率档 0-7（7=最大+10dBm，默认7）
    int rxSensitivity;          // 接收灵敏度档 0-2（0=标准 1=高 2=最高，默认1）
    int brightness;//屏幕亮度
    long autoSleepTime;//自动休眠时间
    long autoScreenOffTime;//自动息屏时间
    bool APEnabled;
    bool WifiEnabled;
    String APName;
    String APPassword;
    String WifiName;
    String WifiPassword;
    int bootAnim;               // 开机动画样式 0=关闭(极速) 1=波浪LOGO 2=扫描线 3=星空 4=打字机 5=矩阵雨，默认1
    // MQTT/Home Assistant配置
    bool MQTTEnabled;           // MQTT是否启用
    String MQTTServer;          // MQTT服务器地址
    int MQTTPort;               // MQTT端口
    String MQTTUsername;        // MQTT用户名
    String MQTTPassword;        // MQTT密码
};

#define RF_SLOT_COUNT       100 // 射频槽位总数 1..100
#define IR_SLOT_COUNT       100 // 红外槽位总数 1..100
#define SIGNAL_SLOT_COUNT   100 // 兼容旧宏（射频/红外共用已废弃，保留供旧代码编译期兼容）

// 主页 slotMap 编码：0=空，1..100=RF槽位，101..200=IR槽位（IR索引+偏移）
#define SLOT_IR_OFFSET      100
inline bool SlotIsIR(int slot) { return slot > SLOT_IR_OFFSET && slot <= SLOT_IR_OFFSET + IR_SLOT_COUNT; }
inline bool SlotIsRF(int slot) { return slot >= 1 && slot <= RF_SLOT_COUNT; }
inline SignalKind SlotToKind(int slot) { return SlotIsIR(slot) ? SIG_IR : SIG_RF; }
inline int SlotToIndex(int slot) { return SlotIsIR(slot) ? (slot - SLOT_IR_OFFSET) : slot; }
inline int KindIndexToSlot(SignalKind kind, int index) { return kind == SIG_IR ? (index + SLOT_IR_OFFSET) : index; }

class DataStore
{
public:
    DataStore();
    ~DataStore();

    // Serializes a backup snapshot or commit with NVS and RAW slot updates.
    class StorageGuard {
    public:
        explicit StorageGuard(DataStore& store, TickType_t wait = pdMS_TO_TICKS(2000))
            : mutex(store.preferencesMutex), locked(mutex && xSemaphoreTakeRecursive(mutex, wait) == pdTRUE) {}
        ~StorageGuard() { if (locked) xSemaphoreGiveRecursive(mutex); }
        StorageGuard(const StorageGuard&) = delete;
        StorageGuard& operator=(const StorageGuard&) = delete;
        explicit operator bool() const { return locked; }

    private:
        SemaphoreHandle_t mutex;
        bool locked;
    };

    static constexpr const char* SettingsNamespace = "RadioData";
    static constexpr const char* SignalsNamespace = "SignalData";
    static constexpr const char* MqttNamespace = "mqtt";
    static bool DecodeSignalBlob(const uint8_t* data, size_t size, SignalEntry& entry);
    static int EncodeRfSignalBlob(const SignalEntry& entry, uint8_t* data, size_t size);
    bool RenameIRSignal(int index, const String& name);
    void NotifyStorageRestored() { ++remoteConfigRevision; ++activeHomeRevision; }

    // ==================== 信号存储（NVS，RF/IR独立槽位） ====================
    bool begin();                 // 初始化信号存储 + 旧 sig_N 迁移 + slotMap编码迁移
    bool SaveSignal(int index, const SignalEntry& entry, const uint16_t* irEdges = nullptr); // 按 entry.kind 路由到 rf_/ir_
    SignalEntry ReadSignal(SignalKind kind, int index);   // 按类型读取
    SignalEntry ReadSignal(int index);   // 兼容旧调用：先RF再IR
    bool SignalExists(SignalKind kind, int index);
    bool SignalExists(int index);        // 兼容：任一类型存在即true
    bool DeleteSignal(SignalKind kind, int index);
    bool DeleteSignal(int index);        // 兼容：删除RF与IR同索引（仅命中域）
    bool ReadIREdges(int index, uint16_t* buffer, uint32_t bufferCount, IRSignalMeta* meta = nullptr); // 仅IR域（index=1..100）
    int  GetSignalCount(SignalKind kind);
    int  GetSignalCount();               // 总和
    void ClearAllSignals(SignalKind kind);
    void ClearAllSignals();              // 清空两种类型

    // ==================== 快捷键（NVS） ====================
    void SaveQuickKey(QuickKey keyData);
    QuickKey LoadQuickKey();

    // ==================== 系统配置（NVS） ====================
    void SaveSystemConfig(SystemConfig systemConfig);
    SystemConfig LoadSystemConfig();
    void ClearAllData(); // 清空所有存储数据（配置+信号，恢复出厂用）

    // WiFi配置相关方法
    bool SaveWiFiConfig(const String& ssid, const String& password);
    bool LoadWiFiConfig(String& ssid, String& password);
    void ClearWiFiConfig();

    // MQTT/HA配置相关方法
    bool SaveMQTTConfig(MQTTMode mode, const String& server, uint16_t port, const String& username, const String& password);
    bool LoadMQTTConfig(MQTTMode& mode, String& server, uint16_t& port, String& username, String& password);
    void ClearMQTTConfig();
    bool HasMQTTConfig();

    // ==================== 多主页配置（NVS，与系统配置同命名空间） ====================
    bool LoadHomeProfile(int id, HomeProfile& out);       // id 0..7（内部做旧布局值迁移）
    bool SaveHomeProfile(const HomeProfile& profile);
    bool DeleteHomeProfile(int id);
    int  CreateHomeProfile(const String& name, uint8_t layout); // 成功返回 id，满额返回 -1
    int  GetHomeProfileCount();                            // 已保存的主页数量
    std::vector<HomeProfile> ListHomeProfiles();            // 已保存的主页列表
    int  GetActiveHomeProfile();                           // 当前激活主页 id（读取 HPROF_ACT）
    bool SetActiveHomeProfile(int id);
    uint32_t GetRemoteConfigRevision() const { return remoteConfigRevision.load(); }
    uint32_t GetActiveHomeRevision() const { return activeHomeRevision.load(); }

private:
    std::atomic<uint32_t> remoteConfigRevision{0};
    std::atomic<uint32_t> activeHomeRevision{0};
    // 互斥锁以确保线程安全（信号blob与NVS配置共用）
    SemaphoreHandle_t preferencesMutex;
    // 信号blob序列化/反序列化（NVS存储格式）
    static int  serializeSignal(const SignalEntry& entry, const uint16_t* irEdges, uint8_t* buf, int bufSize);
    static bool deserializeSignal(const uint8_t* buf, int len, SignalEntry& entry, uint16_t* irEdges, uint32_t edgeBufCount);
};

#endif
