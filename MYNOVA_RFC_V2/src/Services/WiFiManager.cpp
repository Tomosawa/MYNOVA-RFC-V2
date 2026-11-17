/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

#include "WiFiManager.h"
#include "DataStore.h"
#include "HAManager.h"
#include "../Util/TaskConfig.h"
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_bt.h>
#include "../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "WIFIM"

extern DataStore dataStore;
extern HAManager haManager;

WiFiManager::WiFiManager() : scanning(false), scanComplete(false), apStarted(false),
                              connected(false), workerTaskHandle(NULL), cmdQueue(NULL),
                              mqttPending(false) {
    mutex = xSemaphoreCreateMutex();
}

WiFiManager::~WiFiManager() {
    if (workerTaskHandle != NULL) {
        vTaskDelete(workerTaskHandle);
    }
    if (cmdQueue != NULL) {
        vQueueDelete(cmdQueue);
    }
    if (mutex != NULL) {
        vSemaphoreDelete(mutex);
    }
}

// 空安全加锁: 其他全局对象(如uiPageHome)可能在WiFiManager构造函数之前
// 就调用isAPStarted()等查询, 此时mutex为NULL只能直通(构造期单线程, 无竞争)
bool WiFiManager::lock(TickType_t timeout) {
    if (mutex == NULL) return true;
    return xSemaphoreTake(mutex, timeout) == pdTRUE;
}

void WiFiManager::unlock() {
    if (mutex == NULL) return;
    xSemaphoreGive(mutex);
}

void WiFiManager::init() {
    LOG_I("WiFiManager: 初始化");

    // ── WiFi事件诊断(热点DHCP问题排查用, 只打印不改变行为) ──
    // 手机连热点后对照日志判断:
    //   AP_STACONNECTED 出现但手机仍"获取IP失败" → DHCP服务层问题
    //   CONNECTED/DISCONNECTED 反复抖动 → 关联层不稳(STA后台扫描信道漂移/信号弱)
    //   无任何 AP_STA* 事件 → 手机根本没关联进来(密码/加密不匹配)
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        switch (event) {
            case ARDUINO_EVENT_WIFI_AP_START:
                LOG_I("[WIFI-EVT] softAP已启动");
                break;
            case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
                LOG_I("[WIFI-EVT] 手机已关联到热点 MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                      info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
                      info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
                      info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5]);
                LOG_I("[WIFI-EVT] 当前已关联站点数: %d", (int)WiFi.softAPgetStationNum());
                break;
            case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
                LOG_W("[WIFI-EVT] 站点已断开热点 MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                      info.wifi_ap_stadisconnected.mac[0], info.wifi_ap_stadisconnected.mac[1],
                      info.wifi_ap_stadisconnected.mac[2], info.wifi_ap_stadisconnected.mac[3],
                      info.wifi_ap_stadisconnected.mac[4], info.wifi_ap_stadisconnected.mac[5]);
                break;
            case ARDUINO_EVENT_WIFI_SCAN_DONE:
                LOG_D("[WIFI-EVT] STA后台扫描完成(扫描期间AP信道漂移/beacon暂停)");
                break;
            case ARDUINO_EVENT_WIFI_STA_START:
                LOG_I("[WIFI-EVT] STA已启动");
                break;
            case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
                LOG_W("[WIFI-EVT] STA断开(reason=%d)", (int)info.wifi_sta_disconnected.reason);
                break;
            case ARDUINO_EVENT_WIFI_STA_GOT_IP:
                LOG_I("[WIFI-EVT] STA获得IP: %s", WiFi.localIP().toString().c_str());
                break;
            default:
                break;
        }
    });

    // 关闭WiFi省电睡眠: core 3.x下STA空闲时modem sleep会造成RF静默窗口,
    // 吞掉AP的DHCP下行应答(热点"一直获取IP"间歇性失败的嫌疑源之一)
    WiFi.setSleep(false);

    // 默认彻底关闭所有射频（省电+避免2.4G对CC1101/红外及音频的干扰），按需再开
    // 先做最轻量的WIFI_OFF，再由disableAllRadio兜底关蓝牙控制器
    // (注: 不在mode=OFF时调softAPConfig——3.x下属无效暂存, AP网段改由
    //  softAP()官方默认路径初始化, 见handleAPStart)
    WiFi.mode(WIFI_OFF);
    WiFi.setHostname("MYNOVA_RFC");
    disableAllRadio();

    // 持久WiFiWorker(取代原scan/connect/AP三个一次性任务):
    // 钉系统核Core0——WiFi协议栈/lwIP原生驻Core0, 网络操作归位系统核,
    // 不再与UI/业务核抢资源(规划§4.3), 也消灭了反复建删任务的堆抖动。
    cmdQueue = xQueueCreate(6, sizeof(WiFiCommand));
    xTaskCreatePinnedToCore(
        wifiWorkerTask,
        TASK_WIFI_NAME,
        TASK_WIFI_STACK,
        this,
        TASK_PRIO_NET,
        &workerTaskHandle,
        TASK_CORE_SYSTEM
    );
    LOG_I("WiFiManager: WiFiWorker已启动 (Core0)");
}

void WiFiManager::postCommand(const WiFiCommand& cmd) {
    if (cmdQueue == NULL) {
        LOG_W("WiFiManager: 命令队列未就绪, 命令丢弃");
        return;
    }
    if (xQueueSend(cmdQueue, &cmd, 0) != pdTRUE) {
        LOG_W("WiFiManager: 命令队列已满, 命令丢弃");
    }
}

bool WiFiManager::beginTalk(uint8_t channel) {
    std::unique_lock<std::mutex> radio(radioMutex, std::try_to_lock);
    if (!radio.owns_lock() || talkActive) return false;

    talkResumeMode = WiFi.getMode();
    lock();
    talkRestoreAP = (talkResumeMode & WIFI_AP) && apStarted;
    talkReconnectSTA = (talkResumeMode & WIFI_STA) && (WiFi.isConnected() || !currentSSID.isEmpty());
    unlock();

    uint8_t current = 0;
    wifi_second_chan_t secondary;
    if ((WiFi.isConnected() || talkRestoreAP) &&
        esp_wifi_get_channel(&current, &secondary) == ESP_OK && current == channel) {
        if (!WiFi.mode(static_cast<wifi_mode_t>(talkResumeMode | WIFI_STA))) return false;
        talkRetuned = false;
        talkActive = true;
        return true;
    }

    if (talkRestoreAP && esp_wifi_get_config(WIFI_IF_AP, &talkAPConfig) != ESP_OK) return false;
    talkAutoReconnect = WiFi.getAutoReconnect();
    talkRetuned = true;
    talkActive = true;
    WiFi.setAutoReconnect(false);
    if (!WiFi.mode(WIFI_STA) ||
        (talkReconnectSTA && !WiFi.disconnect(false, false, 500)) ||
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        restoreTalkNetwork();
        return false;
    }

    lock();
    apStarted = false;
    connected = false;
    mqttPending = false;
    unlock();
    return true;
}

void WiFiManager::restoreTalkNetwork() {
    talkActive = false;
    bool modeReady = WiFi.mode(talkResumeMode);
    if (!modeReady) LOG_E("WiFiManager: 对讲后恢复WiFi模式失败");
    if (!talkRetuned) return;

    bool apReady = modeReady && talkRestoreAP &&
                   esp_wifi_set_config(WIFI_IF_AP, &talkAPConfig) == ESP_OK;
    if (talkRestoreAP && !apReady) LOG_E("WiFiManager: 对讲后恢复AP失败");
    WiFi.setAutoReconnect(talkAutoReconnect);
    if (modeReady && talkReconnectSTA && !WiFi.isConnected() && !WiFi.reconnect()) {
        LOG_E("WiFiManager: 对讲后重新连接STA失败");
    }

    lock();
    apStarted = apReady;
    connected = WiFi.isConnected();
    unlock();
}

void WiFiManager::endTalk() {
    std::lock_guard<std::mutex> radio(radioMutex);
    if (talkActive) restoreTalkNetwork();
}

// ==================== WiFiWorker: 命令串行处理 ====================

void WiFiManager::wifiWorkerTask(void* parameter) {
    WiFiManager* manager = static_cast<WiFiManager*>(parameter);
    WiFiCommand cmd;
    while (true) {
        if (xQueueReceive(manager->cmdQueue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        std::unique_lock<std::mutex> radio(manager->radioMutex);
        while (manager->talkActive && !manager->radioOffRequested) {
            radio.unlock();
            vTaskDelay(pdMS_TO_TICKS(20));
            radio.lock();
        }
        switch (cmd.type) {
            case CmdType::SCAN:
                if (manager->radioOffRequested) {
                    LOG_W("WiFiWorker: 射频已请求关闭, 丢弃SCAN命令");
                    break;
                }
                manager->handleScan();
                break;
            case CmdType::CONNECT:
                if (manager->radioOffRequested) {
                    LOG_W("WiFiWorker: 射频已请求关闭, 丢弃CONNECT命令");
                    break;
                }
                manager->handleConnect(cmd.ssid, cmd.password);
                break;
            case CmdType::AP_START:
                if (manager->radioOffRequested) {
                    LOG_W("WiFiWorker: 射频已请求关闭, 丢弃AP_START命令");
                    break;
                }
                manager->handleAPStart(cmd.ssid, cmd.password);
                break;
        }
    }
}

// ── SCAN: 原scanTask逻辑 ──
void WiFiManager::handleScan() {
    LOG_I("WiFiWorker: 开始扫描WiFi...");

    // 设备端配网扫描兜底: 射频彻底OFF时打不开STA, 先按需开启
    // AP已启动则保持AP_STA(不断AP), 否则仅STA, 避免多开射频浪费功耗
    if (WiFi.getMode() == WIFI_OFF) {
        lock();
        bool apOn = apStarted;
        unlock();
        WiFi.mode(apOn ? WIFI_AP_STA : WIFI_STA);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    lock();
    scanning = true;
    scanComplete = false;
    scanResults.clear();
    unlock();

    int n = WiFi.scanNetworks();

    lock();
    if (n == 0) {
        LOG_W("WiFiWorker: 未找到WiFi网络");
    } else {
        LOG_I("WiFiWorker: 找到 %d 个网络", n);
        scanResults.clear();
        for (int i = 0; i < n; ++i) {
            WiFiScanResult result;
            result.ssid = WiFi.SSID(i);
            result.rssi = WiFi.RSSI(i);
            result.encryptionType = WiFi.encryptionType(i);
            result.isScanning = false;
            result.networksFound = n;
            scanResults.push_back(result);
        }
    }
    scanning = false;
    scanComplete = true;
    unlock();

    WiFi.scanDelete();
}

// ── CONNECT: 原wifiConnectTask逻辑 ──
void WiFiManager::handleConnect(const char* ssid, const char* password) {
    LOG_I("WiFiWorker: 开始连接WiFi (SSID: %s)", ssid);

    vTaskDelay(pdMS_TO_TICKS(100));

    // 连接前确保STA/AP_STA模式在线(AP不断流, 纯OFF时按需开启STA)
    // 注意: WiFi.begin前必须先配mode, 否则AP_STA/AP会掉线; AP已启动时保持AP_STA
    wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_OFF) {
        lock();
        bool apOn = apStarted;
        unlock();
        if (apOn) WiFi.mode(WIFI_AP_STA);
        else WiFi.mode(WIFI_STA);
        vTaskDelay(pdMS_TO_TICKS(150));
    } else if (mode == WIFI_AP) {
        WiFi.mode(WIFI_AP_STA);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    WiFi.begin(ssid, password);

    // 等待连接（最多10秒）
    int timeout = 20;
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(500));
        timeout--;
    }

    if (WiFi.status() == WL_CONNECTED) {
        LOG_I("WiFiWorker: WiFi连接成功, IP: %s", WiFi.localIP().toString().c_str());

        lock();
        currentSSID = ssid;
        currentPassword = password;
        connected = true;
        // 只置旗：MQTT建连搬到loop()慢路径（worker内禁止同步connectAuto，
        // 否则DNS+TCP数秒阻塞会饿死SCAN/AP_START命令）。门禁见loop()：
        // STA实连 + NVS有MQTT配置才连，否则直接丢弃。
        mqttPending = true;
        unlock();
    } else {
        LOG_E("WiFiWorker: WiFi连接失败");
        lock();
        connected = false;
        mqttPending = false;
        unlock();
    }
}

// ── AP_START: 原apSetupTask逻辑 ──
void WiFiManager::handleAPStart(const char* ssid, const char* password) {
    LOG_I("WiFiWorker: 开始启动AP热点 (SSID: %s)", ssid);

    vTaskDelay(pdMS_TO_TICKS(100));

    // 确保是AP_STA模式
    WiFi.mode(WIFI_AP_STA);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 启动AP: 不再手动softAPConfig(10.0.0.1), 改用core官方默认网段
    // 192.168.4.1/24 + DHCP池(192.168.4.2起)——由softAP()内部保证IP与
    // DHCP池一致, 规避core 3.x静态IP路径下"DHCP运行却不应答"的疑似bug
    // (2026-09排查: 手机关联成功但多数拿不到IP; APModePage的IP显示为动态取值)
    bool result = WiFi.softAP(ssid, password);
    vTaskDelay(pdMS_TO_TICKS(100));

    if (result) {
        LOG_I("WiFiWorker: AP热点启动成功, IP: %s", WiFi.softAPIP().toString().c_str());

        // ── AP DHCP服务器诊断+兜底 ──
        // 现象(2026-09排查): 手机关联成功但DHCP无响应, 反复关联断开拿不到IP。
        // 检查softAP自带的DHCP服务是否真的处于STARTED; 未启动则显式拉起。
        esp_netif_t* apNetif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (apNetif != nullptr) {
            esp_netif_dhcp_status_t st;
            if (esp_netif_dhcps_get_status(apNetif, &st) == ESP_OK) {
                // 枚举: 0=INIT 1=STARTED 2=STOPPED
                LOG_I("WiFiWorker: AP DHCP服务状态=%d (1=运行中)", (int)st);
                if (st != ESP_NETIF_DHCP_STARTED) {
                    esp_err_t e = esp_netif_dhcps_start(apNetif);
                    if (e == ESP_OK) {
                        LOG_I("WiFiWorker: DHCP服务未运行, 已手动拉起");
                    } else {
                        LOG_E("WiFiWorker: DHCP手动拉起失败 err=%d", (int)e);
                    }
                }
            } else {
                LOG_W("WiFiWorker: 无法查询AP DHCP状态");
            }
            // 打印netif实际IP(确认softAPConfig的10.0.0.1真正生效)
            esp_netif_ip_info_t ipInfo;
            if (esp_netif_get_ip_info(apNetif, &ipInfo) == ESP_OK) {
                LOG_I("WiFiWorker: AP netif IP=%s mask=%s gw=%s",
                      IPAddress(ipInfo.ip.addr).toString().c_str(),
                      IPAddress(ipInfo.netmask.addr).toString().c_str(),
                      IPAddress(ipInfo.gw.addr).toString().c_str());
            }
        } else {
            LOG_W("WiFiWorker: 未找到AP netif(WIFI_AP_DEF)");
        }

        lock();
        apSSID = ssid;
        apPassword = password;
        apStarted = true;
        unlock();
    } else {
        LOG_E("WiFiWorker: AP热点启动失败");
        lock();
        apStarted = false;
        unlock();
    }
}

// ==================== WiFi 扫描相关(对外接口不变) ====================

void WiFiManager::startScan() {
    lock();
    bool busy = scanning;
    unlock();
    if (busy) {
        LOG_W("WiFiManager: 扫描已在进行中");
        return;
    }

    radioOffRequested = false;  // 用户主动扫描=要用射频, 解除关闭请求

    WiFiCommand cmd = {};
    cmd.type = CmdType::SCAN;
    cmd.ssid[0] = '\0';
    cmd.password[0] = '\0';
    postCommand(cmd);
}

bool WiFiManager::isScanComplete() {
    lock();
    bool complete = scanComplete;
    unlock();
    return complete;
}

void WiFiManager::getScanResultsCopy(std::vector<WiFiScanResult> &out) {
    lock();
    out = scanResults;
    unlock();
}

String WiFiManager::getScanResultJson() {
    lock();

    JsonDocument doc;
    JsonArray wifiArray = doc["WIFI_SCAN"].to<JsonArray>();

    for (const auto& result : scanResults) {
        JsonObject wifi = wifiArray.add<JsonObject>();
        wifi["SSID"] = result.ssid;
        wifi["RSSI"] = result.rssi;
        wifi["Encryption"] = (result.encryptionType == WIFI_AUTH_OPEN) ? "Open" : "Encrypted";
    }

    doc["scanning"] = scanning;
    doc["count"] = scanResults.size();

    unlock();

    String output;
    serializeJson(doc, output);
    return output;
}

// ==================== WiFi STA 连接相关 ====================

bool WiFiManager::connectToWiFi(const String& ssid, const String& password) {
    std::lock_guard<std::mutex> radio(radioMutex);
    if (talkActive) return false;
    // 阻塞式连接(调试/配网路径用; 页面/服务一律走connectToWiFiAsync)
    LOG_I("WiFiManager: 连接WiFi: %s", ssid.c_str());

    WiFi.begin(ssid.c_str(), password.c_str());

    int attempts = 0;
    const int maxAttempts = 20; // 10秒超时

    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
        delay(500);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        LOG_I("WiFiManager: WiFi连接成功, IP: %s", WiFi.localIP().toString().c_str());
        lock();
        currentSSID = ssid;
        currentPassword = password;
        connected = true;
        unlock();
        return true;
    } else {
        LOG_E("WiFiManager: WiFi连接失败!");
        return false;
    }
}

void WiFiManager::connectToWiFiAsync(const String& ssid, const String& password) {
    LOG_I("WiFiManager: 异步连接WiFi(投递CONNECT命令)");
    radioOffRequested = false;  // 用户主动连接=要用射频, 解除关闭请求
    WiFiCommand cmd = {};
    cmd.type = CmdType::CONNECT;
    strlcpy(cmd.ssid, ssid.c_str(), sizeof(cmd.ssid));
    strlcpy(cmd.password, password.c_str(), sizeof(cmd.password));
    postCommand(cmd);
}

void WiFiManager::disconnect() {
    std::lock_guard<std::mutex> radio(radioMutex);
    LOG_I("WiFiManager: 断开WiFi");
    if (talkActive) {
        talkResumeMode = static_cast<wifi_mode_t>(talkResumeMode & ~WIFI_STA);
        talkReconnectSTA = false;
    }
    WiFi.disconnect(!talkActive);
    lock();
    connected = false;
    mqttPending = false;
    currentSSID = "";
    currentPassword = "";
    unlock();
    if (!talkActive) applyModeAfterChange();
}

bool WiFiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String WiFiManager::getConnectionInfo() {
    JsonDocument doc;

    if (WiFi.status() == WL_CONNECTED) {
        doc["connected"] = true;
        doc["ssid"] = WiFi.SSID();
        doc["ip"] = WiFi.localIP().toString();
        doc["rssi"] = WiFi.RSSI();
    } else {
        doc["connected"] = false;
    }

    String output;
    serializeJson(doc, output);
    return output;
}

void WiFiManager::shutdown() {
    std::lock_guard<std::mutex> radio(radioMutex);
    if (talkActive && talkRetuned) WiFi.setAutoReconnect(talkAutoReconnect);
    talkActive = false;
    LOG_I("WiFiManager: 关闭WiFi");
    // 停止AP
    if (apStarted) {
        WiFi.softAPdisconnect(true);
        lock();
        apStarted = false;
        unlock();
    }
    // 断开WiFi并关闭(worker若在扫描/连接中, 因射频关闭会自然结束)
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    lock();
    connected = false;
    unlock();
}

void WiFiManager::disableAllRadio() {
    LOG_I("WiFiManager: 彻底关闭无线射频（WiFi+蓝牙）省电/防干扰");

    // 防复活: 置关闭请求标志并清空队列在途命令(SCAN/CONNECT/AP_START),
    // 否则worker随后消费残留命令会把刚关掉的射频重新拉起
    // (标志由用户主动开启入口startScan/connectToWiFi*/startAPAsync清除)
    radioOffRequested = true;
    if (cmdQueue != NULL) {
        xQueueReset(cmdQueue);
    }

    // 复用 shutdown 彻底关闭 WiFi
    shutdown();

    // 确保 WiFi 驱动层真正停止（WiFi.mode(WIFI_OFF) 已调 esp_wifi_stop，此处兜底忽略错误）
    esp_wifi_stop();

    // 彻底关闭蓝牙：Host(NimBLE/Bluedroid) + Controller，释放控制器内存
#if defined(CONFIG_BT_ENABLED)
    // Arduino 层 btStop()：内部按 Host stop → controller disable/deinit 顺序处理，
    // 适合 NimBLE/Bluedroid 两种协议栈，失败(未初始化)也安全
    extern bool btStop() __attribute__((weak));
    if (btStop) {
        btStop();
    }
    // 控制器兜底：若 btStop 未能完全 deinit，则显式 disable + deinit
    esp_bt_controller_status_t cst = esp_bt_controller_get_status();
    if (cst == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        esp_bt_controller_disable();
        cst = esp_bt_controller_get_status();
    }
    if (cst == ESP_BT_CONTROLLER_STATUS_INITED) {
        esp_bt_controller_deinit();
    }
    // 释放控制器内存（省 RAM，且保证射频段彻底静默）
    // controller 未 deinit 时该调用返回 ESP_ERR_INVALID_STATE，忽略即可
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
#endif

    LOG_I("WiFiManager: 无线射频已彻底关闭 (WiFi OFF + BT controller deinit)");
}

// ==================== AP 热点相关(对外接口不变) ====================

void WiFiManager::startAPAsync(const String& ssid, const String& password) {
    LOG_I("WiFiManager: 异步启动AP热点(投递AP_START命令)");
    radioOffRequested = false;  // 用户主动开AP=要用射频, 解除关闭请求
    WiFiCommand cmd = {};
    cmd.type = CmdType::AP_START;
    strlcpy(cmd.ssid, ssid.c_str(), sizeof(cmd.ssid));
    strlcpy(cmd.password, password.c_str(), sizeof(cmd.password));
    postCommand(cmd);
}

void WiFiManager::stopAP() {
    std::lock_guard<std::mutex> radio(radioMutex);
    LOG_I("WiFiManager: 停止AP热点");
    if (talkActive) {
        talkResumeMode = static_cast<wifi_mode_t>(talkResumeMode & ~WIFI_AP);
        talkRestoreAP = false;
    }
    if (WiFi.getMode() & WIFI_AP) WiFi.softAPdisconnect(true);
    lock();
    apStarted = false;
    unlock();
    if (!talkActive) applyModeAfterChange();
}

// mode归位：断STA/停AP后把射频档位降到最低（AP_STA→AP/STA→OFF），
// 双关时彻底OFF（省电+防CC1101干扰+让WebService的OFF判断可信）。
// 注意：仅在STA已断/AP已停后调用；开启路径（handleConnect/handleAPStart）不管。
void WiFiManager::applyModeAfterChange() {
    lock();
    bool apOn = apStarted;
    unlock();
    bool staUp = (WiFi.status() == WL_CONNECTED);
    if (apOn && staUp) WiFi.mode(WIFI_AP_STA);
    else if (apOn) WiFi.mode(WIFI_AP);
    else if (staUp) WiFi.mode(WIFI_STA);
    else WiFi.mode(WIFI_OFF);
}

bool WiFiManager::consumeMqttPending() {
    lock();
    bool p = mqttPending;
    mqttPending = false;
    unlock();
    return p;
}

bool WiFiManager::isAPStarted() {
    lock();
    bool started = apStarted;
    unlock();
    return started;
}

String WiFiManager::getAPIP() {
    return WiFi.softAPIP().toString();
}

bool WiFiManager::isWifiActive() {
    // 只要射频未彻底OFF，或AP/STA已起，即视为活跃（会有2.4G杂音）
    wifi_mode_t mode = WiFi.getMode();
    if (mode != WIFI_OFF) return true;
    // 兼容：mode为OFF但内部标志仍显示已连接/AP已起的极端情况
    lock();
    bool sta = connected || (WiFi.status() == WL_CONNECTED);
    bool ap  = apStarted;
    unlock();
    return sta || ap;
}

bool WiFiManager::isBtActive() {
#if defined(CONFIG_BT_ENABLED)
    esp_bt_controller_status_t st = esp_bt_controller_get_status();
    return st == ESP_BT_CONTROLLER_STATUS_ENABLED;
#else
    return false;
#endif
}

bool WiFiManager::isAnyRadioActive() {
    return isWifiActive() || isBtActive();
}

// ==================== 配置存储相关 ====================

bool WiFiManager::saveConfig(const String& ssid, const String& password) {
    return dataStore.SaveWiFiConfig(ssid, password);
}

bool WiFiManager::loadConfig(String& ssid, String& password) {
    return dataStore.LoadWiFiConfig(ssid, password);
}

void WiFiManager::clearConfig() {
    dataStore.ClearWiFiConfig();
}
