/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

#ifndef __WIFIMANAGER_H__
#define __WIFIMANAGER_H__

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <atomic>
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

struct WiFiScanResult {
    String ssid;
    int32_t rssi;
    uint8_t encryptionType;
    bool isScanning;
    int networksFound;
};

// WiFiManager —— P3改造: scan/connect/AP 三个一次性任务合并为一个
// 持久WiFiWorker(钉Core0, 命令队列驱动), 消灭反复建删任务与跨核WiFi调用。
// 对外接口签名与行为保持不变(调用方零改动)。
class WiFiManager {
public:
    WiFiManager();
    ~WiFiManager();

    // 初始化WiFi管理器(创建命令队列+启动worker, 开机调一次)
    void init();

    // ==================== WiFi STA 相关 ====================
    // 异步扫描WiFi网络(投递SCAN命令)
    void startScan();

    // 检查扫描是否完成
    bool isScanComplete();

    // 拷贝扫描结果供设备端页面使用(线程安全)
    void getScanResultsCopy(std::vector<WiFiScanResult> &out);

    // 获取扫描结果的JSON字符串
    String getScanResultJson();

    // 连接到指定的WiFi网络（阻塞式, 调试/配网路径用）
    bool connectToWiFi(const String& ssid, const String& password);

    // 异步连接WiFi（非阻塞, 投递CONNECT命令）
    void connectToWiFiAsync(const String& ssid, const String& password);

    // 断开WiFi连接(同步, 直接执行)
    void disconnect();

    // 检查WiFi连接状态
    bool isConnected();

    // 获取当前连接的WiFi信息
    String getConnectionInfo();

    // 完全关闭WiFi（用于休眠）
    void shutdown();

    // 彻底关闭所有无线射频（WiFi + 蓝牙），用于启动时省电/避免RF干扰
    void disableAllRadio();

    // ESP-NOW uses STA. Preserve an existing same-channel network, or restore it after a retune.
    bool beginTalk(uint8_t channel);
    void endTalk();

    // STA连成功后置旗：loop()慢路径消费并执行MQTT建连门禁（worker内禁同步连）
    bool consumeMqttPending();

    // 查询射频状态（用于录音/播放前提示）
    bool isWifiActive();      // STA 已连接或 AP 已启动/WiFi mode != OFF 即视为活跃
    bool isBtActive();        // 蓝牙控制器已使能
    bool isAnyRadioActive();  // WiFi 或蓝牙任一活跃

    // ==================== AP 热点相关 ====================
    // 启动AP热点（异步, 投递AP_START命令）
    void startAPAsync(const String& ssid, const String& password);

    // 停止AP热点（同步）
    void stopAP();

    // 检查AP是否已启动
    bool isAPStarted();

    // 获取AP的IP地址
    String getAPIP();

    // ==================== 配置存储相关 ====================
    // 保存WiFi配置
    bool saveConfig(const String& ssid, const String& password);

    // 加载WiFi配置
    bool loadConfig(String& ssid, String& password);

    // 清除WiFi配置
    void clearConfig();

private:
    // 命令类型
    enum class CmdType : uint8_t { SCAN, CONNECT, AP_START };
    struct WiFiCommand {
        CmdType type;
        char ssid[33];
        char password[65];
    };

    // 持久worker任务(Core0): 命令队列串行执行 scan/connect/AP
    static void wifiWorkerTask(void* parameter);
    // worker内的命令处理(原三个一次性任务函数的逻辑)
    void handleScan();
    void handleConnect(const char* ssid, const char* password);
    void handleAPStart(const char* ssid, const char* password);
    // 投递命令(队列满时丢弃并打印)
    void postCommand(const WiFiCommand& cmd);

    void restoreTalkNetwork(); // radioMutex must be held.
    std::mutex radioMutex;
    bool talkActive = false;
    bool talkRetuned = false;
    bool talkReconnectSTA = false;
    bool talkAutoReconnect = false;
    bool talkRestoreAP = false;
    wifi_mode_t talkResumeMode = WIFI_OFF;
    wifi_config_t talkAPConfig{};

    // 扫描结果
    std::vector<WiFiScanResult> scanResults;

    // 状态标志
    bool scanning;
    bool scanComplete;
    bool apStarted;

    // worker任务与命令队列
    TaskHandle_t workerTaskHandle;
    QueueHandle_t cmdQueue;
    // STA连成功时置旗（worker置、loop()消），worker内禁止同步做MQTT建连
    bool mqttPending;
    // 射频关闭请求(防复活): disableAllRadio置位并清空在途命令, worker处理
    // SCAN/CONNECT/AP_START前检查, 置位则丢弃; 用户主动开启入口(startScan/
    // connectToWiFi*/startAPAsync)清除——否则音乐播放弹窗关掉的射频会被
    // 队列中残留的连接命令重新拉起
    std::atomic<bool> radioOffRequested{false};
    // mode归位：按 apStarted/STA实连状态把射频档位降到最低（AP/STA/OFF）
    void applyModeAfterChange();

    // 互斥锁(全局构造期可能被其他全局对象提前调用, 一律走lock()/unlock()空安全封装)
    SemaphoreHandle_t mutex;

    // 空安全加锁: mutex为NULL(构造期)时按单线程直通, 启动后正常加锁
    bool lock(TickType_t timeout = portMAX_DELAY);
    void unlock();

    // 当前连接/AP信息
    String currentSSID;
    String currentPassword;
    String apSSID;
    String apPassword;
    bool connected;
};

#endif
