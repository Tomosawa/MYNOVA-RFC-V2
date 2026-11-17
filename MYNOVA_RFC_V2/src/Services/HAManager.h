/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef HAMANAGER_H
#define HAMANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <atomic>
#include <bitset>
#include <mutex>
#include "DataStore.h"
#include "../Drivers/RadioHelper.h"
#include "../Drivers/IRHelper.h"
#include "../Drivers/BatteryManager.h"
/**
 * Home Assistant MQTT集成管理器
 * 通过MQTT协议与Home Assistant通信
 * 使用MQTT Discovery自动发现设备和实体
 */
class HAManager {
public:
    HAManager();
    
    /**
     * 初始化HA管理器
     * @param dataStore DataStore实例指针
     * @param radioHelper RadioHelper实例指针
     */
    void init(DataStore* dataStore, RadioHelper* radioHelper, IRHelper* irHelper);
    
    /**
     * 设置BatteryManager实例
     * @param batteryManager BatteryManager实例指针
     */
    void setBatteryManager(class BatteryManager* batteryManager);
    
    /**
     * 自动发现并连接到Home Assistant MQTT服务器
     * 按保存的模式使用 mDNS 自动发现或指定的服务器
     * @return 是否连接成功
     */
    bool connectAuto();
    
    /**
     * 连接到Home Assistant MQTT服务器（使用DataStore中保存的配置）
     * @return 是否连接成功
     */
    bool connect();
    
    /**
     * 连接到Home Assistant MQTT服务器（手动指定参数）
     * @param haServerIP HA服务器IP地址
     * @param port MQTT端口（默认1883）
     * @param username MQTT用户名（可选）
     * @param password MQTT密码（可选）
     * @param saveConfig 是否保存配置到DataStore（默认true）
     * @return 是否连接成功
     */
    bool connect(const char* haServerIP, uint16_t port = 1883, 
                 const char* username = nullptr, const char* password = nullptr,
                 bool saveConfig = true);
    
    /**
     * 使用mDNS发现Home Assistant MQTT服务器
     * @param server 输出参数：发现的服务器IP
     * @param port 输出参数：发现的服务器端口
     * @return 是否发现成功
     */
    bool discoverMQTTServer(String& server, uint16_t& port);
    
    /**
     * 断开与HA的连接
     */
    void disconnect();
    
    /**
     * 循环处理MQTT消息
     * 需要在主循环中调用
     */
    void loop();
    
    /**
     * 检查是否已连接
     */
    bool isConnected();
    
    /**
     * 检查是否有保存的MQTT配置
     */
    bool hasSavedConfig();
    
    /**
     * 设置设备信息
     */
    void setDeviceInfo(const char* deviceName, const char* model, 
                      const char* manufacturer, const char* swVersion);

private:
    WiFiClient wifiClient;
    PubSubClient mqttClient;
    // Web 配置端点与 Arduino loop 共用同一个 MQTT 客户端。
    std::mutex mqttMutex;
    std::atomic<bool> mqttConnected{false};
    
    DataStore* pDataStore;
    RadioHelper* pRadioHelper;
    IRHelper* pIRHelper;
    BatteryManager* pBatteryManager;
    
    // 设备信息
    String deviceName;
    String deviceModel;
    String deviceManufacturer;
    String deviceSWVersion;
    String deviceID;  // 唯一设备ID（基于MAC地址）
    
    // MQTT主题前缀
    String topicPrefix;
    String availabilityTopic;
    
    // 连接参数
    String mqttServer;
    uint16_t mqttPort;
    String mqttUsername;
    String mqttPassword;
    
    unsigned long lastReconnectAttempt;
    unsigned long lastDiscoveryAttempt;
    unsigned long lastBatteryPublish;
    uint32_t publishedRevision = 0;
    uint32_t publishedHomeRevision = 0;
    bool discoveryPending = true;
    bool homeStatePending = true;
    std::bitset<SLOT_IR_OFFSET + IR_SLOT_COUNT + 1> publishedSlots;
    
    /**
     * MQTT回调函数
     */
    static void mqttCallback(char* topic, byte* payload, unsigned int length);
    
    /**
     * 处理接收到的MQTT命令
     */
    void handleCommand(const String& topic, const String& payload);
    bool publishAvailability(bool online);
    bool publishDiscovery();
    bool publishEntity(const char* component, const String& objectId, JsonDocument& doc);
    bool publishButtonDiscovery(int slot, const SignalEntry& data);
    bool publishHomeDiscovery();
    bool publishHomeState();
    bool publishBatteryDiscovery();
    void publishBatteryState();
    
    /**
     * 生成唯一设备ID
     */
    String generateDeviceID();
    
    /**
     * 重连MQTT服务器
     */
    bool reconnect();
    bool connectSavedConfig();
    bool connectToBroker(const char* host, uint16_t port, const char* username, const char* password);
    void closeConnection();
};

#endif // HAMANAGER_H
