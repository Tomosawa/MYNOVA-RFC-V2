/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#include "HAManager.h"
#include "../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "HA"

// 静态实例指针，用于回调函数
static HAManager* g_haManagerInstance = nullptr;

HAManager::HAManager() : mqttClient(wifiClient) {
    pDataStore = nullptr;
    pRadioHelper = nullptr;
    pIRHelper = nullptr;
    pBatteryManager = nullptr;
    mqttPort = 1883;
    lastReconnectAttempt = 0;
    lastDiscoveryAttempt = 0;
    lastBatteryPublish = 0;
    
    // 设置默认设备信息
    deviceName = "MYNOVA RFC";
    deviceModel = "RF Remote Controller";
    deviceManufacturer = "MYNOVA";
    deviceSWVersion = "1.0.0";
    
    g_haManagerInstance = this;
}

void HAManager::init(DataStore* dataStore, RadioHelper* radioHelper, IRHelper* irHelper) {
    pDataStore = dataStore;
    pRadioHelper = radioHelper;
    pIRHelper = irHelper;
    
    // 生成唯一设备ID（基于MAC地址）
    deviceID = generateDeviceID();
    
    // 设置MQTT主题前缀
    topicPrefix = "homeassistant/button/mynova_rfc_" + deviceID;
    availabilityTopic = topicPrefix + "/availability";
    
    LOG_I("HAManager: 初始化完成");
    LOG_I("设备ID: %s", deviceID.c_str());
    LOG_I("主题前缀: %s", topicPrefix.c_str());
    
    // 初始化mDNS
    if (!MDNS.begin("mynova_rfc")) {
        LOG_E("HAManager: mDNS初始化失败");
    } else {
        LOG_I("HAManager: mDNS初始化成功");
        MDNS.addService("http", "tcp", 80);  // 广播Web服务
    }
    
    // 检查是否有保存的MQTT配置
    if (pDataStore && pDataStore->HasMQTTConfig()) {
        LOG_I("HAManager: 检测到已保存的MQTT配置");
    }
}

void HAManager::setDeviceInfo(const char* name, const char* model, 
                              const char* manufacturer, const char* swVersion) {
    deviceName = name;
    deviceModel = model;
    deviceManufacturer = manufacturer;
    deviceSWVersion = swVersion;
}

void HAManager::setBatteryManager(BatteryManager* batteryManager) {
    pBatteryManager = batteryManager;
}

bool HAManager::connectAuto() {
    std::lock_guard<std::mutex> lock(mqttMutex);
    return connectSavedConfig();
}

bool HAManager::connectSavedConfig() {
    // 检查WiFi连接
    if (WiFi.status() != WL_CONNECTED) {
        LOG_E("HAManager: WiFi未连接，无法连接MQTT");
        return false;
    }
    
    if (!pDataStore) {
        LOG_E("HAManager: DataStore未初始化");
        return false;
    }
    
    LOG_I("==================================================");
    LOG_I("HAManager: 开始自动连接流程");
    LOG_I("==================================================");
    
    // 加载配置
    MQTTMode mode;
    String server;
    uint16_t port;
    String username;
    String password;
    
    if (!pDataStore->LoadMQTTConfig(mode, server, port, username, password)) {
        LOG_W("✗ 未找到MQTT配置");
        LOG_I("提示：请通过Web界面配置 Home Assistant");
        LOG_I("==================================================");
        return false;
    }
    
    LOG_I("配置模式: %s", mode == MQTTMode::AUTO ? "auto" : "manual");
    
    // 根据模式决定连接方式
    if (mode == MQTTMode::AUTO) {
        // 自动模式：使用 mDNS 发现
        LOG_I("步骤1: 使用mDNS自动发现...");
        String discoveredServer;
        uint16_t discoveredPort;
        
        if (discoverMQTTServer(discoveredServer, discoveredPort)) {
            LOG_I("✓ mDNS发现成功！");
            LOG_I("  服务器: %s:%d", discoveredServer.c_str(), discoveredPort);
            
            if (discoveredServer.length() > 0 && discoveredServer != "0.0.0.0") {
                // 使用发现的服务器 + 配置的用户名密码
                if (connectToBroker(discoveredServer.c_str(), discoveredPort,
                           username.length() > 0 ? username.c_str() : nullptr,
                           password.length() > 0 ? password.c_str() : nullptr)) {
                    LOG_I("✓ 自动发现并连接成功！");
                    LOG_I("==================================================");
                    return true;
                } else {
                    LOG_E("✗ 连接失败，请检查用户名密码");
                }
            }
        } else {
            LOG_E("✗ mDNS发现失败");
        }
    } else if (mode == MQTTMode::MANUAL) {
        // 手动模式：使用配置的服务器地址
        LOG_I("步骤1: 使用手动配置的服务器...");
        
        if (server.length() > 0) {
            LOG_I("  服务器: %s:%d", server.c_str(), port);
            
            if (connectToBroker(server.c_str(), port,
                       username.length() > 0 ? username.c_str() : nullptr,
                       password.length() > 0 ? password.c_str() : nullptr)) {
                LOG_I("✓ 连接成功！");
                LOG_I("==================================================");
                return true;
            } else {
                LOG_E("✗ 连接失败，请检查服务器地址和认证信息");
            }
        } else {
            LOG_E("✗ 未配置服务器地址");
        }
    }
    
    LOG_I("==================================================");
    LOG_E("✗ 连接失败");
    LOG_I("提示：请检查 Home Assistant 配置");
    LOG_I("  1. 确认 Mosquitto broker 已启动");
    LOG_I("  2. 检查用户名密码是否正确");
    LOG_I("  3. 或在 Mosquitto 中启用匿名访问");
    LOG_I("==================================================");
    
    return false;
}

bool HAManager::connect() {
    // connect() 现在调用 connectAuto()
    return connectAuto();
}

bool HAManager::discoverMQTTServer(String& server, uint16_t& port) {
    LOG_I("--------------------");
    LOG_I("开始mDNS服务发现");
    LOG_I("--------------------");
    
    // mDNS查询需要时间，等待一下
    delay(500);
    
    // 策略1: 查询标准MQTT服务 (_mqtt._tcp)
    LOG_I("[1/3] 查询标准MQTT服务: _mqtt._tcp");
    int n = MDNS.queryService("mqtt", "tcp");
    if (n > 0) {
        LOG_I("  ✓ 找到 %d 个MQTT服务", n);
        // ESP32 mDNS API: 使用 address() 或 IP()
        IPAddress ip = MDNS.address(0);
        if (ip != IPAddress(0, 0, 0, 0)) {
            server = ip.toString();
            port = MDNS.port(0);
            LOG_I("  使用: %s:%d", server.c_str(), port);
            return true;
        }
    }
    LOG_W("  ✗ 未找到");
    
    // 策略2: 直接解析 homeassistant.local
    LOG_I("[2/3] 解析 homeassistant.local");
    IPAddress haIP = MDNS.queryHost("homeassistant");
    if (haIP != IPAddress(0, 0, 0, 0)) {
        server = haIP.toString();
        port = 1883;  // 使用默认MQTT端口
        LOG_I("  ✓ 解析成功: %s:%d", server.c_str(), port);
        return true;
    }
    LOG_W("  ✗ 解析失败");
    
    // 策略3: 查询Home Assistant HTTP服务，假设MQTT在同一主机
    LOG_I("[3/3] 查询Home Assistant HTTP服务");
    n = MDNS.queryService("http", "tcp");
    if (n > 0) {
        LOG_I("  找到 %d 个HTTP服务", n);
        for (int i = 0; i < n; i++) {
            String hostname = MDNS.hostname(i);
            LOG_I("  服务 %d: %s", i + 1, hostname.c_str());
            
            // 检查是否是Home Assistant
            if (hostname.indexOf("homeassistant") >= 0 || 
                hostname.indexOf("hassio") >= 0) {
                IPAddress ip = MDNS.address(i);
                if (ip != IPAddress(0, 0, 0, 0)) {
                    server = ip.toString();
                    port = 1883;  // 假设MQTT在同一主机的1883端口
                    LOG_I("  ✓ 疑似HA服务: %s:%d", server.c_str(), port);
                    return true;
                }
            }
        }
        LOG_W("  ✗ 未找到HA相关服务");
    } else {
        LOG_W("  ✗ 未找到HTTP服务");
    }
    
    LOG_I("--------------------");
    LOG_E("mDNS发现失败");
    LOG_I("--------------------");
    return false;
}

bool HAManager::connect(const char* haServerIP, uint16_t port, 
                        const char* username, const char* password,
                        bool saveConfig) {
    std::lock_guard<std::mutex> lock(mqttMutex);
    if (saveConfig && pDataStore) {
        pDataStore->SaveMQTTConfig(MQTTMode::MANUAL, haServerIP, port,
                                  username ? username : "", password ? password : "");
    }
    return connectToBroker(haServerIP, port, username, password);
}

bool HAManager::connectToBroker(const char* host, uint16_t port, const char* username, const char* password) {
    const char* user = username ? username : "";
    const char* pass = password ? password : "";
    if (mqttClient.connected() && mqttServer == host && mqttPort == port &&
        mqttUsername == user && mqttPassword == pass) {
        return true;
    }

    closeConnection();
    mqttServer = host;
    mqttPort = port;
    mqttUsername = user;
    mqttPassword = pass;
    // PubSubClient 保存域名指针，必须指向连接期间持续有效的字符串。
    mqttClient.setServer(mqttServer.c_str(), mqttPort);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(3072);  // 容纳 8 个主页选项及其 JSON 转义
    
    LOG_I("HAManager: 连接到 %s:%d", mqttServer.c_str(), mqttPort);
    return reconnect();
}

bool HAManager::reconnect() {
    // 无服务器地址时静默失败（未配置），避免 state -2 刷屏
    if (mqttServer.length() == 0) {
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        //Serial.println("HAManager: WiFi未连接");
        return false;
    }
    
    String clientId = "mynova_rfc_" + deviceID;
    
    bool connected = false;
    if (mqttUsername.length() > 0) {
        connected = mqttClient.connect(clientId.c_str(), 
                                       mqttUsername.c_str(), 
                                       mqttPassword.c_str(),
                                       availabilityTopic.c_str(),
                                       0, true, "offline");
    } else {
        connected = mqttClient.connect(clientId.c_str(),
                                       availabilityTopic.c_str(),
                                       0, true, "offline");
    }
    
    if (!connected) {
        mqttConnected = false;
        LOG_E("HAManager: MQTT连接失败，错误代码: %d", mqttClient.state());
        return false;
    }

    String commandTopic = topicPrefix + "/+/command";
    if (!mqttClient.subscribe(commandTopic.c_str()) ||
        !mqttClient.subscribe("homeassistant/status") || !publishAvailability(true)) {
        LOG_E("HAManager: MQTT订阅或在线状态发布失败");
        closeConnection();
        return false;
    }

    // 首次连接也清理空槽位，处理设备离线时删除或旧版本残留的实体。
    publishedSlots.set();
    discoveryPending = true;
    lastDiscoveryAttempt = millis() - 2000;
    mqttConnected = true;
    LOG_I("HAManager: MQTT连接成功");
    return true;
}

void HAManager::disconnect() {
    std::lock_guard<std::mutex> lock(mqttMutex);
    closeConnection();
    mqttServer = "";
    mqttUsername = "";
    mqttPassword = "";
    lastReconnectAttempt = millis();
}

void HAManager::closeConnection() {
    if (mqttClient.connected()) {
        if (publishAvailability(false)) mqttClient.disconnect();
        else wifiClient.stop();  // 发送失败时由 broker 发布遗嘱
        LOG_I("HAManager: 已断开MQTT连接");
    }
    mqttConnected = false;
}

void HAManager::loop() {
    std::unique_lock<std::mutex> lock(mqttMutex, std::try_to_lock);
    if (!lock.owns_lock()) return;

    // 未配置MQTT时直接静默返回，不尝试连接、不打印报错
    if (mqttServer.length() == 0) {
        // 兜底：NVS里有配置但内存中还没有（比如刚从备份恢复），按5秒节流触发一次自动连接
        // WiFi未连接时不探测NVS（省去无意义读取）；完全未配置时静默返回
        unsigned long now = millis();
        if (pDataStore && WiFi.status() == WL_CONNECTED && (now - lastReconnectAttempt > 5000)) {
            lastReconnectAttempt = now;
            if (pDataStore->HasMQTTConfig()) {
                if (connectSavedConfig()) lastReconnectAttempt = 0;
            }
        }
        return;
    }
    if (!mqttClient.connected()) {
        mqttConnected = false;
        // WiFi断开时不做MQTT重连尝试（connect()内虽会快速失败，但5秒一次的
        // DNS/TCP超时仍会抖动；等STA恢复后由worker置旗/兜底路径再连）
        if (WiFi.status() != WL_CONNECTED) return;
        unsigned long now = millis();
        // 每5秒尝试重连一次
        if (now - lastReconnectAttempt > 5000) {
            lastReconnectAttempt = now;
            if (reconnect()) {
                lastReconnectAttempt = 0;
            }
        }
    } else {
        mqttConnected = mqttClient.loop();
        if (!mqttConnected) return;
        
        unsigned long now = millis();
        
        // NVS 写入来自设备、Web 和备份恢复。发布失败或发布期间有更新时重试。
        uint32_t revision = pDataStore ? pDataStore->GetRemoteConfigRevision() : 0;
        if ((discoveryPending || revision != publishedRevision) && now - lastDiscoveryAttempt >= 2000) {
            lastDiscoveryAttempt = now;
            if (publishDiscovery()) {
                publishedRevision = revision;
                discoveryPending = false;
                homeStatePending = true;
            } else {
                discoveryPending = true;
                LOG_W("HAManager: MQTT Discovery 同步失败，将重试");
            }
            publishBatteryState();
            lastBatteryPublish = now;
        }

        uint32_t homeRevision = pDataStore ? pDataStore->GetActiveHomeRevision() : 0;
        if (!discoveryPending && (homeStatePending || homeRevision != publishedHomeRevision)) {
            if (publishHomeState()) {
                publishedHomeRevision = homeRevision;
                homeStatePending = false;
            }
        }
        
        // 每30秒发布电池电量
        if (now - lastBatteryPublish > 30000) {
            publishBatteryState();
            lastBatteryPublish = now;
        }
    }
}

bool HAManager::isConnected() {
    return mqttConnected.load();
}

bool HAManager::publishAvailability(bool online) {
    const char* status = online ? "online" : "offline";
    return mqttClient.publish(availabilityTopic.c_str(), status, true);
}

bool HAManager::publishDiscovery() {
    if (!pDataStore) return false;
    for (int slot = 1; slot <= SLOT_IR_OFFSET + IR_SLOT_COUNT; ++slot) {
        if (!SlotIsRF(slot) && !SlotIsIR(slot)) continue;
        SignalEntry entry = pDataStore->ReadSignal(SlotToKind(slot), SlotToIndex(slot));
        if (entry.name.length() == 0 && !publishedSlots.test(slot)) continue;
        if (!publishButtonDiscovery(slot, entry)) return false;
        publishedSlots.set(slot, entry.name.length() > 0);
    }

    return publishHomeDiscovery() && publishBatteryDiscovery();
}

bool HAManager::publishEntity(const char* component, const String& objectId, JsonDocument& doc) {
    doc["unique_id"] = "mynova_rfc_" + deviceID + "_" + objectId;
    doc["availability_topic"] = availabilityTopic;

    JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"][0] = deviceID;
    device["name"] = deviceName;
    device["model"] = deviceModel;
    device["manufacturer"] = deviceManufacturer;
    device["sw_version"] = deviceSWVersion;
    device["configuration_url"] = "http://" + WiFi.localIP().toString();

    String configTopic = String("homeassistant/") + component + "/mynova_rfc_" + deviceID + "/" + objectId + "/config";
    String payload;
    return !doc.overflowed() && serializeJson(doc, payload) == measureJson(doc) &&
           mqttClient.publish(configTopic.c_str(), payload.c_str(), true);
}

bool HAManager::publishButtonDiscovery(int slot, const SignalEntry& data) {
    String objectId = "button_" + String(slot);
    if (data.name.length() == 0) {
        // 空的保留消息同时移除 HA 实体和 broker 上的旧 Discovery 配置。
        String configTopic = topicPrefix + "/" + objectId + "/config";
        return mqttClient.publish(configTopic.c_str(), "", true);
    }

    JsonDocument doc;
    doc["name"] = data.name;
    doc["command_topic"] = topicPrefix + "/" + objectId + "/command";
    doc["payload_press"] = "PRESS";
    doc["retain"] = false;
    doc["icon"] = SlotIsIR(slot) ? "mdi:remote-tv" : "mdi:remote";
    return publishEntity("button", objectId, doc);
}

static String homeOption(const HomeProfile& profile) {
    // 主页名称可以重复，用槽位号区分选项。
    return String(profile.id + 1) + ". " + profile.name;
}

bool HAManager::publishHomeDiscovery() {
    auto homes = pDataStore->ListHomeProfiles();
    if (homes.empty()) {
        String configTopic = "homeassistant/select/mynova_rfc_" + deviceID + "/active_home/config";
        return mqttClient.publish(configTopic.c_str(), "", true);
    }

    JsonDocument doc;
    doc["name"] = "Active home";
    doc["icon"] = "mdi:view-dashboard";
    doc["command_topic"] = topicPrefix + "/active_home/command";
    doc["state_topic"] = topicPrefix + "/active_home/state";
    doc["retain"] = false;
    JsonArray options = doc["options"].to<JsonArray>();
    for (const auto& profile : homes) {
        options.add(homeOption(profile));
    }

    return publishEntity("select", "active_home", doc);
}

bool HAManager::publishHomeState() {
    HomeProfile profile;
    String state;
    if (pDataStore->LoadHomeProfile(pDataStore->GetActiveHomeProfile(), profile)) {
        state = homeOption(profile);
    }
    String stateTopic = topicPrefix + "/active_home/state";
    return mqttClient.publish(stateTopic.c_str(), state.c_str(), true);
}

String HAManager::generateDeviceID() {
    // 使用MAC地址生成唯一ID
    uint8_t mac[6];
    WiFi.macAddress(mac);
    
    char macStr[13];
    sprintf(macStr, "%02X%02X%02X%02X%02X%02X", 
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return String(macStr);
}

void HAManager::mqttCallback(char* topic, byte* payload, unsigned int length) {
    // 按字节复制，只接受完整的文本命令。
    if (g_haManagerInstance && memchr(payload, '\0', length) == nullptr) {
        g_haManagerInstance->handleCommand(topic, String(reinterpret_cast<const char*>(payload), length));
    }
}

bool HAManager::hasSavedConfig() {
    if (!pDataStore) {
        return false;
    }
    return pDataStore->HasMQTTConfig();
}

void HAManager::handleCommand(const String& topic, const String& payload) {
    if (topic == "homeassistant/status" && payload == "online") {
        discoveryPending = true;
        return;
    }
    if (!pDataStore) return;

    if (topic == topicPrefix + "/active_home/command") {
        long number = payload.toInt();
        HomeProfile profile;
        if (number >= 1 && number <= HOME_PROFILE_MAX &&
            pDataStore->LoadHomeProfile(number - 1, profile) && payload == homeOption(profile) &&
            !pDataStore->SetActiveHomeProfile(profile.id)) {
            LOG_W("HAManager: 主页切换失败");
        }
        // 发布实际选中项，覆盖过期选项或写入失败后的状态。
        homeStatePending = true;
        return;
    }

    String prefix = topicPrefix + "/button_";
    if (payload != "PRESS" || !topic.startsWith(prefix)) return;

    long slot = topic.substring(prefix.length()).toInt();
    if (slot < 1 || slot > SLOT_IR_OFFSET + IR_SLOT_COUNT ||
        topic != prefix + String(slot) + "/command") return;

    bool sent = false;
    if (SlotIsIR(slot) && pIRHelper) {
        sent = pIRHelper->SendFromSlot(SlotToIndex(slot));
    } else if (SlotIsRF(slot) && pRadioHelper) {
        SignalEntry entry = pDataStore->ReadSignal(SIG_RF, slot);
        if (entry.name.length() > 0) sent = pRadioHelper->SendData(entry.rf);
    }
    if (!sent) LOG_W("HAManager: 槽位 %ld 发射失败或发射器忙", slot);
}

bool HAManager::publishBatteryDiscovery() {
    JsonDocument doc;
    doc["name"] = "Battery";
    doc["state_topic"] = topicPrefix + "/battery/state";
    doc["unit_of_measurement"] = "%";
    doc["device_class"] = "battery";
    doc["state_class"] = "measurement";
    doc["icon"] = "mdi:battery";
    return publishEntity("sensor", "battery", doc);
}

void HAManager::publishBatteryState() {
    if (!mqttClient.connected()) {
        return;
    }
    
    if (!pBatteryManager) {
        return;
    }
    
    float voltage = pBatteryManager->getLastVoltage();
    if (voltage <= 0) return;
    float percent = pBatteryManager->calculateBatteryPercent(voltage);
    
    String stateTopic = topicPrefix + "/battery/state";
    String payload = String(percent, 1);
    
    bool success = mqttClient.publish(stateTopic.c_str(), payload.c_str(), true);
    
    if (success) {
        LOG_I("HAManager: 电池电量已发布 - %.1f%%", percent);
    }
}
