/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#include "WebService.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include "DataStore.h"
#include "SystemSetting.h"
#include "../Drivers/RadioHelper.h"
#include "../Drivers/IRHelper.h"
#include "../Drivers/BatteryManager.h"
#include "BackupManager.h"
#include "HAManager.h"
#include "../Util/HeapMonitor.h"
#include "../Util/Log.h"
#include "../../Version.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "WEB"

extern DataStore dataStore;
extern SystemSetting systemSetting;
extern RadioHelper radioHelper;
extern IRHelper irHelper;
extern HAManager haManager;
extern BatteryManager batteryManager;
void handleRequest(AsyncWebServerRequest *request){}
void handleUploadRequest(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){}

// ──────────────────────────────────────────────────────────────────────────────
// Web端点内部辅助
// ──────────────────────────────────────────────────────────────────────────────

// 延迟重启任务函数（先让AsyncWebServer把响应发完再重启，避免响应被截断）
static void webRestartTask(void* pv)
{
    vTaskDelay(pdMS_TO_TICKS((uint32_t)(uintptr_t)pv));
    ESP.restart();
}

static void scheduleRestart(uint32_t delayMs)
{
    xTaskCreatePinnedToCore(webRestartTask, "web_restart", 2048,
                            (void*)(uintptr_t)delayMs, 5, nullptr, tskNO_AFFINITY);
}

// 恢复出厂任务（响应发出后执行：清空NVS全部数据 → 重启，与设备端FactoryResetPage一致）
static void factoryResetTask(void* pv)
{
    vTaskDelay(pdMS_TO_TICKS(300));   // 等待响应发出
    dataStore.ClearAllData();        // 清空 RadioData + SignalData 命名空间
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP.restart();
}

// 信号名称校验：非空且UTF-8字节数≤31（NVS名称字段31字节，约10个汉字）
static bool validSignalName(const String& name)
{
    return name.length() > 0 && name.length() <= 31;
}

// RFSignalData → 大写HEX字符串（MSB first，与radiodata/list一致）
static String rfDataToHex(const RFSignalData& rf)
{
    uint16_t bytes = (rf.bitCount + 7) / 8;
    if (bytes > 32) bytes = 32;
    String dataHex = "";
    char hexBuf[4];
    for (uint16_t b = 0; b < bytes; b++) {
        snprintf(hexBuf, sizeof(hexBuf), "%02X", rf.data[b]);
        dataHex += hexBuf;
    }
    return dataHex;
}

WebService::WebService():
server(80),
webOwnedRx(false),
webOwnedIr(false),
hasCapturedRf(false),
lastIrEdges(nullptr),
hasLastIrMeta(false),
otaUpdateOK(false),
serverRunning(false),
pendingStart(false),
routesRegistered(false)
{
}

void WebService::init(WiFiManager *wifiMgr)
{
    pWiFiManager = wifiMgr;
    LOG_I("WebService: 创建AsyncWebServer实例");
}

void WebService::serverStart()
{
    if (serverRunning) {
        LOG_I("WebService: 服务器已在运行，跳过重复启动");
        return;
    }

    // 无网卡不建站：WiFi/AP 全关时 WiFi.mode()==WIFI_OFF，
    // 此时 AsyncWebServer::begin() 会在 AsyncTCP/lwIP 未就绪时触发
    // xQueueSemaphoreTake 断言（queue.c:1709）直接崩溃。必须先挡掉。
    SystemConfig sc = dataStore.LoadSystemConfig();
    bool needNet = sc.WifiEnabled || sc.APEnabled;
    if (!needNet) {
        LOG_I("WebService: WiFi/AP均关闭，跳过HTTP服务器启动（零监听/零功耗）");
        return;
    }
    if (WiFi.getMode() == WIFI_OFF) {
        LOG_W("WebService: 射频仍为OFF，推迟HTTP服务器启动（等STA/AP就绪）");
        pendingStart = true;
        return;
    }
    LOG_I("WebService: 开始启动Web服务器");
    
    // 初始化LittleFS文件系统
    LOG_I("WebService: 挂载LittleFS文件系统");
    if (!LittleFS.begin(false))
    {
        LOG_E("WebService: LittleFS挂载失败");
        return;
    }
    LOG_I("WebService: LittleFS挂载成功");

    if (!routesRegistered) {
    // 配置静态文件服务
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setFilter(
        [](AsyncWebServerRequest* request) {
            const String& path = request->url();
            if (path.indexOf("/.") >= 0 || path.indexOf('%') >= 0 || path.indexOf('\\') >= 0) return false;
            return path == "/" || path.endsWith(".html") || path.endsWith(".js") ||
                   path.endsWith(".css") || path.endsWith(".woff2") || path.endsWith(".ico") ||
                   path.endsWith(".svg") || path.endsWith(".png");
        });
    
    // 注册API路由
    LOG_I("WebService: 注册API路由");
    
    // WIFI相关接口
    server.on(AsyncURIMatcher("/api/wifiscan"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&WebService::handleWIFIScanRequest, this, std::placeholders::_1));
    server.on(AsyncURIMatcher("/api/wifisave"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleWIFISaveRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));
    server.on(AsyncURIMatcher("/api/wificlear"), HTTP_DELETE, (ArRequestHandlerFunction)std::bind(&WebService::handleWIFIClearRequest, this, std::placeholders::_1));
    server.on(AsyncURIMatcher("/api/wificonnect"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&WebService::handleWIFIConnectRequest, this, std::placeholders::_1));
    server.on(AsyncURIMatcher("/api/wifienable"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleWIFIEnableRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));
    server.on(AsyncURIMatcher("/api/apwifiinfo"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&WebService::handleAPWIFIInfoRequest, this, std::placeholders::_1));
    server.on(AsyncURIMatcher("/api/apsave"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleAPSaveRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));
    server.on(AsyncURIMatcher("/api/apenable"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleAPEnableRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));
    
    // 无线遥控数据管理接口
    server.on(AsyncURIMatcher("/api/radiodata/list"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&WebService::handleRadioDataListRequest, this, std::placeholders::_1));
    server.on(AsyncURIMatcher("/api/radiodata/add"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleRadioDataAddRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));
    server.on("/api/radiodata/update", HTTP_POST,
              [this](AsyncWebServerRequest* request, JsonVariant& body) {
                  handleRadioDataUpdateRequest(request, body);
              });
    server.on(AsyncURIMatcher("/api/radiodata/delete"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleRadioDataDeleteRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));
    server.on(AsyncURIMatcher("/api/radiodata/send"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleRadioDataSendRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));
    
    // MQTT/HA配置接口
    server.on(AsyncURIMatcher("/api/mqtt/config"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&WebService::handleMQTTConfigGetRequest, this, std::placeholders::_1));
    server.on(AsyncURIMatcher("/api/mqtt/config"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleMQTTConfigSaveRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));
    server.on(AsyncURIMatcher("/api/mqtt/config"), HTTP_DELETE, (ArRequestHandlerFunction)std::bind(&WebService::handleMQTTConfigClearRequest, this, std::placeholders::_1));
    server.on(AsyncURIMatcher("/api/mqtt/connect"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&WebService::handleMQTTConnectRequest, this, std::placeholders::_1));
    server.on(AsyncURIMatcher("/api/mqtt/status"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&WebService::handleMQTTStatusRequest, this, std::placeholders::_1));

    // 备份管理接口（TF卡）
    server.on(AsyncURIMatcher("/api/backup/list"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&WebService::handleBackupListRequest, this, std::placeholders::_1));
    server.on("/api/backup/export", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleBackupExportRequest(request);
    });
    server.on("/api/backup/restore", HTTP_POST,
              [this](AsyncWebServerRequest* request, JsonVariant& body) {
                  handleBackupRestoreRequest(request, body);
              }).setMaxContentLength(512);
    server.on("/api/backup/delete", HTTP_POST,
              [this](AsyncWebServerRequest* request, JsonVariant& body) {
                  handleBackupDeleteRequest(request, body);
              }).setMaxContentLength(512);

    // 日志级别查询/设置(远程排障开关, 级别持久化到NVS)
    server.on(AsyncURIMatcher("/api/loglevel"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&WebService::handleLogLevelRequest, this, std::placeholders::_1));

    // 无线遥控数据单条查询（handler已存在，此前未注册路由——补齐）
    server.on(AsyncURIMatcher("/api/radiodata/get"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&WebService::handleRadioDataGetRequest, this, std::placeholders::_1));

    // ==================== 设备状态/系统设置接口 ====================
    server.on(AsyncURIMatcher("/api/status"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&WebService::handleStatusRequest, this, std::placeholders::_1));
    server.on(AsyncURIMatcher("/api/settings"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&WebService::handleSettingsGetRequest, this, std::placeholders::_1));
    server.on(AsyncURIMatcher("/api/settings"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleSettingsPostRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));
    server.on(AsyncURIMatcher("/api/reboot"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleRebootRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));
    server.on(AsyncURIMatcher("/api/factoryreset"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleFactoryResetRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));

    // ==================== RF接收捕获接口(Web嗅探工作流) ====================
    server.on(AsyncURIMatcher("/api/rf/rx/start"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleRfRxStartRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));
    server.on(AsyncURIMatcher("/api/rf/rx/status"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&WebService::handleRfRxStatusRequest, this, std::placeholders::_1));
    server.on(AsyncURIMatcher("/api/rf/rx/stop"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleRfRxStopRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));
    server.on(AsyncURIMatcher("/api/rf/rx/save"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleRfRxSaveRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));

    // ==================== IR学习捕获接口 ====================
    server.on(AsyncURIMatcher("/api/ir/learn/start"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleIrLearnStartRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));
    server.on(AsyncURIMatcher("/api/ir/learn/status"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&WebService::handleIrLearnStatusRequest, this, std::placeholders::_1));
    server.on(AsyncURIMatcher("/api/ir/learn/cancel"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleIrLearnCancelRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));
    server.on(AsyncURIMatcher("/api/ir/learn/save"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleIrLearnSaveRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));

    // ==================== 多主页管理接口 ====================
    server.on(AsyncURIMatcher("/api/home/list"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&WebService::handleHomeListRequest, this, std::placeholders::_1));
    server.on(AsyncURIMatcher("/api/home/create"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleHomeCreateRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));
    server.on(AsyncURIMatcher("/api/home/save"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleHomeSaveRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));
    server.on(AsyncURIMatcher("/api/home/delete"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleHomeDeleteRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));
    server.on(AsyncURIMatcher("/api/home/activate"), HTTP_POST, handleRequest, handleUploadRequest, (ArBodyHandlerFunction)std::bind(&WebService::handleHomeActivateRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5));

    // ==================== OTA固件升级接口(浏览器上传) ====================
    server.on(AsyncURIMatcher("/api/ota/info"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&WebService::handleOTAInfoRequest, this, std::placeholders::_1));
    server.on(AsyncURIMatcher("/api/ota/upload"), HTTP_POST,
              (ArRequestHandlerFunction)std::bind(&WebService::handleOTAUploadDoneRequest, this, std::placeholders::_1),
              (ArUploadHandlerFunction)std::bind(&WebService::handleOTAUploadRequest, this, std::placeholders::_1,std::placeholders::_2,std::placeholders::_3,std::placeholders::_4,std::placeholders::_5,std::placeholders::_6));
    } // routesRegistered

    LOG_I("WebService: 启动HTTP服务器");
    server.begin();
    serverRunning = true;
    pendingStart = false;
    routesRegistered = true;
    LOG_I("WebService: Web服务器启动成功，监听端口80");
}

void WebService::serverStop()
{
    if (!serverRunning) return;
    server.end();
    serverRunning = false;
    LOG_I("WebService: HTTP服务器已停止");
}

void WebService::processRequest()
{
    // 推迟启动补救：射频就绪（STA已连或AP已起）后自动把Web服务拉起；
    // 用户在运行时打开WiFi/AP开关同样走这里，无需重启。
    if (!serverRunning && pendingStart) {
        bool netReady = (WiFi.status() == WL_CONNECTED) || (WiFi.getMode() & WIFI_AP);
        if (netReady) {
            LOG_I("WebService: 网络已就绪，补启动HTTP服务器");
            pendingStart = false;
            serverStart();
        }
    }
    // 反向同步（节流1秒，避开NVS高频读）：运行时双开关全关 → 主动end释放监听
    static unsigned long lastSync = 0;
    if (serverRunning && millis() - lastSync > 1000) {
        lastSync = millis();
        SystemConfig sc = dataStore.LoadSystemConfig();
        if (!sc.WifiEnabled && !sc.APEnabled) {
            pendingStart = false;
            LOG_I("WebService: 双开关已关闭，停止HTTP服务器");
            serverStop();
        }
    }
}

void WebService::handleWIFIScanRequest(AsyncWebServerRequest *request)
{
    // 使用WiFiManager进行异步扫描
    if (pWiFiManager) {
        if (!pWiFiManager->isScanComplete()) {
            pWiFiManager->startScan();
        }
        request->send(200, "application/json", pWiFiManager->getScanResultJson());
    } else {
        request->send(200, "application/json", "{\"result\":\"failed\",\"message\":\"WiFiManager not initialized\"}");
    }
}

void WebService::handleWIFISaveRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    String jsonStr =(char*)data;
  
    // 打印原始POST数据
    LOG_D("%s", jsonStr.c_str());
    // 解析JSON数据
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error)
    {
        LOG_E("deserializeJson() failed: %s", error.c_str());
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"JSON parsing failed\"}");
        return;
    }
    // 处理解析后的JSON数据
    const char* ssid = doc["SSID"];
    const char* pwd = doc["PWD"];
    if (!ssid || strlen(ssid) == 0) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"SSID required\"}");
        return;
    }

    // 与设备端WiFiModePage共用SystemSetting/DataStore, Web保存后页面实时展示
    systemSetting.setWifiConfig(ssid, pwd ? pwd : "", true);

    // 返回响应
    request->send(200, "application/json", "{\"result\":\"OK\"}");
}

void WebService::handleWIFIClearRequest(AsyncWebServerRequest *request)
{
    // Web清除 = 清空WiFi配置并关闭开关(与设备端展示同步)
    systemSetting.setWifiEnabled(false, false);
    systemSetting.setWifiConfig("", "", true);
    if (pWiFiManager) {
        pWiFiManager->clearConfig();
        pWiFiManager->disconnect();
    }

    // 返回响应
    request->send(200, "application/json", "{\"result\":\"OK\"}");
}

void WebService::handleWIFIConnectRequest(AsyncWebServerRequest *request)
{
    // 以SystemSetting内存配置为准(Web保存走同一份, 旧WiFiManager::loadConfig
    // 读的是同NVS键, 但设备端改动只写SystemConfig, 此处统一用SystemSetting防漂移)
    SystemConfig cfg = systemSetting.getConfig();
    if (cfg.WifiName.length() > 0) {
        if (!cfg.WifiEnabled) {
            systemSetting.setWifiEnabled(true, true);
        } else if (pWiFiManager) {
            pWiFiManager->connectToWiFiAsync(cfg.WifiName, cfg.WifiPassword);
        }
        // 异步连接: 立即返回, 前端轮询/apwifiinfo查看状态
        request->send(200, "application/json", "{\"result\":\"OK\",\"connected\":false,\"connecting\":true}");
        return;
    }

    request->send(200, "application/json", "{\"result\":\"failed\",\"message\":\"No WiFi config\"}");
}


// 无线遥控数据管理接口实现（射频/红外统一信号槽位）
void WebService::handleRadioDataListRequest(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    JsonArray dataArray = doc["data"].to<JsonArray>();

    // 读取所有信号槽位（RF 1..100 + IR 1..100，编码后返回 index 1..100 RF / 101..200 IR）
    for (int i = 1; i <= RF_SLOT_COUNT; i++) {
        SignalEntry entry = dataStore.ReadSignal(SIG_RF, i);
        if (entry.name.length() > 0) {
            JsonObject item = dataArray.add<JsonObject>();
            item["index"] = i;
            item["name"] = entry.name;
            item["kind"] = "RF";
            item["frequency"] = entry.rf.frequency;
            item["bitCount"] = entry.rf.bitCount;
            item["protocolName"] = entry.rf.protocolName;
            item["shortPulse"] = entry.rf.shortPulse;
            item["longPulse"] = entry.rf.longPulse;
            item["syncPulse"] = entry.rf.syncPulse;
            item["value"] = entry.rf.getValue32();
            uint16_t bytes = (entry.rf.bitCount + 7) / 8;
            if (bytes > 32) bytes = 32;
            String dataHex = "";
            char hexBuf[4];
            for (uint16_t b = 0; b < bytes; b++) { snprintf(hexBuf, sizeof(hexBuf), "%02X", entry.rf.data[b]); dataHex += hexBuf; }
            item["dataHex"] = dataHex;
        }
    }
    for (int i = 1; i <= IR_SLOT_COUNT; i++) {
        SignalEntry entry = dataStore.ReadSignal(SIG_IR, i);
        if (entry.name.length() == 0) continue;
        {
            JsonObject item = dataArray.add<JsonObject>();
            item["index"] = KindIndexToSlot(SIG_IR, i);
            item["name"] = entry.name;
            item["kind"] = "IR";
            item["protocolName"] = entry.ir.protocolName;
            item["frameCount"] = entry.ir.frameCount;
            item["totalEdges"] = entry.ir.totalEdges;
        }
    }

    doc["result"] = "OK";
    doc["count"] = dataArray.size();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebService::handleRadioDataAddRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    String jsonStr = String((char*)data).substring(0, len);
    LOG_D("Add Signal: %s", jsonStr.c_str());

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        LOG_E("deserializeJson() failed: %s", error.c_str());
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"JSON parsing failed\"}");
        return;
    }

    // 红外信号必须通过设备学习获得，API仅支持新增射频信号
    String kind = doc["kind"].as<String>();
    if (kind == "IR") {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"IR signals must be learned on device\"}");
        return;
    }

    // 查找第一个空RF位置（IR需设备学习）
    int emptyIndex = -1;
    for (int i = 1; i <= RF_SLOT_COUNT; i++) {
        if (!dataStore.SignalExists(SIG_RF, i)) {
            emptyIndex = i;
            break;
        }
    }

    if (emptyIndex == -1) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"No empty slot available\"}");
        return;
    }

    // 创建新射频信号（CC1101时序参数格式）
    SignalEntry newData;
    newData.kind = SIG_RF;
    newData.name = doc["name"].as<String>();
    newData.rf.frequency = doc["frequency"] | 433.92;
    newData.rf.bitCount = doc["bitCount"] | 24;
    newData.rf.shortPulse = doc["shortPulse"] | 300;
    newData.rf.longPulse = doc["longPulse"] | 900;
    newData.rf.syncPulse = doc["syncPulse"] | 9000;
    // 数据字段：十六进制字符串（MSB first，最多32字节）
    String dataHex = doc["dataHex"].as<String>();
    uint16_t bytes = (newData.rf.bitCount + 7) / 8;
    if (bytes > 32) bytes = 32;
    for (uint16_t i = 0; i < bytes && (i * 2 + 1) < dataHex.length(); i++) {
        char byteStr[3] = {dataHex[i * 2], dataHex[i * 2 + 1], '\0'};
        newData.rf.data[i] = (uint8_t)strtol(byteStr, nullptr, 16);
    }
    String protoName = doc["protocolName"].as<String>();
    strncpy(newData.rf.protocolName, protoName.c_str(), sizeof(newData.rf.protocolName) - 1);

    // 保存数据
    dataStore.SaveSignal(emptyIndex, newData);

    JsonDocument responseDoc;
    responseDoc["result"] = "OK";
    responseDoc["index"] = emptyIndex;

    String output;
    serializeJson(responseDoc, output);
    request->send(200, "application/json", output);
}

void WebService::handleRadioDataUpdateRequest(AsyncWebServerRequest* request, JsonVariant& doc)
{
    int dataIndex = doc["index"].as<int>();
    if (dataIndex < 1 || dataIndex > SLOT_IR_OFFSET + IR_SLOT_COUNT) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid index\"}");
        return;
    }
    if (SlotIsIR(dataIndex)) {
        String name = doc["name"].as<String>();
        name.trim();
        bool ok = dataStore.RenameIRSignal(SlotToIndex(dataIndex), name);
        request->send(ok ? 200 : 400, "application/json",
                      ok ? "{\"result\":\"OK\"}" : "{\"result\":\"failed\",\"message\":\"IR rename failed\"}");
        return;
    }
    SignalEntry updateData = dataStore.ReadSignal(SIG_RF, dataIndex);
    if (updateData.name.length() == 0) {
        updateData.kind = SIG_RF;
    }
    updateData.name = doc["name"].as<String>();

    if (updateData.kind == SIG_RF) {
        // 射频信号：允许更新时序参数
        if (doc["frequency"].is<float>() || doc["frequency"].is<int>()) updateData.rf.frequency = doc["frequency"];
        if (!doc["bitCount"].isNull()) updateData.rf.bitCount = doc["bitCount"];
        if (!doc["shortPulse"].isNull()) updateData.rf.shortPulse = doc["shortPulse"];
        if (!doc["longPulse"].isNull()) updateData.rf.longPulse = doc["longPulse"];
        if (!doc["syncPulse"].isNull()) updateData.rf.syncPulse = doc["syncPulse"];
        if (!doc["dataHex"].isNull()) {
            String dataHex = doc["dataHex"].as<String>();
            memset(updateData.rf.data, 0, sizeof(updateData.rf.data));
            uint16_t bytes = (updateData.rf.bitCount + 7) / 8;
            if (bytes > 32) bytes = 32;
            for (uint16_t i = 0; i < bytes && (i * 2 + 1) < dataHex.length(); i++) {
                char byteStr[3] = {dataHex[i * 2], dataHex[i * 2 + 1], '\0'};
                updateData.rf.data[i] = (uint8_t)strtol(byteStr, nullptr, 16);
            }
        }
    }

    dataStore.SaveSignal(dataIndex, updateData);

    request->send(200, "application/json", "{\"result\":\"OK\"}");
}

void WebService::handleRadioDataDeleteRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    String jsonStr = String((char*)data).substring(0, len);
    LOG_D("Delete Signal: %s", jsonStr.c_str());

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        LOG_E("deserializeJson() failed: %s", error.c_str());
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"JSON parsing failed\"}");
        return;
    }

    int dataIndex = doc["index"].as<int>();
    if (dataIndex < 1 || dataIndex > SLOT_IR_OFFSET + IR_SLOT_COUNT) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid index\"}");
        return;
    }
    if (SlotIsIR(dataIndex)) dataStore.DeleteSignal(SIG_IR, SlotToIndex(dataIndex));
    else dataStore.DeleteSignal(SIG_RF, dataIndex);

    request->send(200, "application/json", "{\"result\":\"OK\"}");
}

void WebService::handleRadioDataGetRequest(AsyncWebServerRequest *request)
{
    if (!request->hasParam("index")) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Missing index parameter\"}");
        return;
    }

    int dataIndex = request->getParam("index")->value().toInt();
    if (dataIndex < 1 || dataIndex > SLOT_IR_OFFSET + IR_SLOT_COUNT) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid index\"}");
        return;
    }

    SignalEntry entry;
    if (SlotIsIR(dataIndex)) entry = dataStore.ReadSignal(SIG_IR, SlotToIndex(dataIndex));
    else entry = dataStore.ReadSignal(SIG_RF, dataIndex);
    JsonDocument doc;
    doc["result"] = "OK";
    doc["index"] = dataIndex;
    doc["name"] = entry.name;
    doc["kind"] = (entry.kind == SIG_IR) ? "IR" : "RF";
    if (entry.name.length() == 0) {
        doc["result"] = "EMPTY";
    } else if (entry.kind == SIG_RF) {
        doc["frequency"] = entry.rf.frequency;
        doc["bitCount"] = entry.rf.bitCount;
        doc["protocolName"] = entry.rf.protocolName;
        doc["shortPulse"] = entry.rf.shortPulse;
        doc["longPulse"] = entry.rf.longPulse;
        doc["syncPulse"] = entry.rf.syncPulse;
        doc["value"] = entry.rf.getValue32();
        // 完整数据HEX
        uint16_t bytes = (entry.rf.bitCount + 7) / 8;
        if (bytes > 32) bytes = 32;
        String dataHex = "";
        char hexBuf[4];
        for (uint16_t b = 0; b < bytes; b++) {
            snprintf(hexBuf, sizeof(hexBuf), "%02X", entry.rf.data[b]);
            dataHex += hexBuf;
        }
        doc["dataHex"] = dataHex;
    } else {
        doc["protocolName"] = entry.ir.protocolName;
        doc["frameCount"] = entry.ir.frameCount;
        doc["totalEdges"] = entry.ir.totalEdges;
        // v2 解码详情：每帧位宽/码值/地址/命令 + 空调状态（旧数据无解码详情，字段为空）
        JsonArray frameBits = doc["frameBits"].to<JsonArray>();
        JsonArray frameValues = doc["frameValues"].to<JsonArray>();
        JsonArray frameAddrs = doc["frameAddrs"].to<JsonArray>();
        JsonArray frameCmds = doc["frameCmds"].to<JsonArray>();
        for (uint8_t i = 0; i < entry.ir.frameCount; i++) {
            frameBits.add(entry.ir.frameBits[i]);
            char valHex[24];
            snprintf(valHex, sizeof(valHex), "%llX", (unsigned long long)entry.ir.frameValue[i]);
            frameValues.add(String(valHex));
            char addrHex[12];
            snprintf(addrHex, sizeof(addrHex), "%X", entry.ir.frameAddr[i]);
            frameAddrs.add(String(addrHex));
            char cmdHex[12];
            snprintf(cmdHex, sizeof(cmdHex), "%X", entry.ir.frameCmd[i]);
            frameCmds.add(String(cmdHex));
        }
        if (entry.ir.acStateDesc[0]) doc["acState"] = entry.ir.acStateDesc;
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}
// 新增的API处理函数 - 添加到WebService.cpp末尾

// WiFi开关控制
void WebService::handleWIFIEnableRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    String jsonStr = String((char*)data).substring(0, len);
    LOG_D("WiFi Enable: %s", jsonStr.c_str());
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    
    if (error) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid JSON\"}");
        return;
    }
    
    bool wifiEnable = doc["WIFI_ENABLE"].as<bool>();
    systemSetting.setWifiEnabled(wifiEnable, true);
    
    request->send(200, "application/json", "{\"result\":\"OK\"}");
}

// 获取AP和WiFi配置信息
void WebService::handleAPWIFIInfoRequest(AsyncWebServerRequest *request)
{
    SystemConfig config = systemSetting.getConfig();

    JsonDocument doc;
    doc["AP_SSID"] = config.APName;
    doc["AP_PWD"] = config.APPassword;
    doc["AP_ENABLE"] = config.APEnabled;
    doc["WIFI_SSID"] = config.WifiName;
    doc["WIFI_PASSWORD"] = config.WifiPassword;
    doc["WIFI_ENABLE"] = config.WifiEnabled;
    // 实时连接状态(设备端页面/前端共用同一真相源)
    doc["WIFI_CONNECTED"] = (WiFi.status() == WL_CONNECTED);
    if (WiFi.status() == WL_CONNECTED) {
        doc["WIFI_IP"] = WiFi.localIP().toString();
        doc["WIFI_RSSI"] = WiFi.RSSI();
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// 保存AP配置
void WebService::handleAPSaveRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    String jsonStr = String((char*)data).substring(0, len);
    LOG_D("Save AP Config: %s", jsonStr.c_str());
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    
    if (error) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid JSON\"}");
        return;
    }
    
    String apSSID = doc["AP_SSID"].as<String>();
    String apPWD = doc["AP_PWD"].as<String>();
    
    systemSetting.setAPConfig(apSSID, apPWD, true);
    
    request->send(200, "application/json", "{\"result\":\"OK\"}");
}

// AP开关控制
void WebService::handleAPEnableRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    String jsonStr = String((char*)data).substring(0, len);
    LOG_D("AP Enable: %s", jsonStr.c_str());
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    
    if (error) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid JSON\"}");
        return;
    }
    
    bool apEnable = doc["AP_ENABLE"].as<bool>();
    systemSetting.setAPEnabled(apEnable, true);
    
    request->send(200, "application/json", "{\"result\":\"OK\"}");
}

// 发送遥控信号
void WebService::handleRadioDataSendRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    String jsonStr = String((char*)data).substring(0, len);
    LOG_D("Send Radio Signal: %s", jsonStr.c_str());
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    
    if (error) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid JSON\"}");
        return;
    }
    
    int dataIndex = doc["index"].as<int>();
    if (dataIndex < 1 || dataIndex > SLOT_IR_OFFSET + IR_SLOT_COUNT) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid index\"}");
        return;
    }

    SignalEntry entry;
    if (SlotIsIR(dataIndex)) entry = dataStore.ReadSignal(SIG_IR, SlotToIndex(dataIndex));
    else entry = dataStore.ReadSignal(SIG_RF, dataIndex);

    if (entry.name.length() == 0) {
        request->send(404, "application/json", "{\"result\":\"failed\",\"message\":\"Data not found\"}");
        return;
    }

    // 按信号类型发送
    bool sent;
    if (entry.kind == SIG_RF) {
        LOG_I("发送信号: %s | RF %.2fMHz %dbits", entry.name.c_str(), entry.rf.frequency, entry.rf.bitCount);
        sent = radioHelper.SendData(entry.rf);
    } else {
        LOG_I("发送信号: %s | IR %d帧", entry.name.c_str(), entry.ir.frameCount);
        sent = irHelper.SendFromSlot(SlotToIndex(dataIndex));
    }
    if (!sent) {
        request->send(503, "application/json", "{\"result\":\"failed\",\"message\":\"Signal transmission failed or transmitter busy\"}");
        return;
    }

    request->send(200, "application/json", "{\"result\":\"OK\",\"message\":\"Signal sent successfully\"}");
}

// ==================== MQTT/HA配置接口实现 ====================

void WebService::handleMQTTConfigGetRequest(AsyncWebServerRequest *request)
{
    MQTTMode mode;
    String server;
    uint16_t port;
    String username;
    String password;
    
    JsonDocument doc;
    
    if (dataStore.LoadMQTTConfig(mode, server, port, username, password)) {
        doc["result"] = "OK";
        doc["mode"] = (mode == MQTTMode::AUTO) ? "auto" : "manual";
        doc["server"] = server;
        doc["port"] = port;
        doc["username"] = username;
        // 不返回密码，用******表示
        doc["password"] = (password.length() > 0) ? "******" : "";
        doc["hasConfig"] = true;
    } else {
        doc["result"] = "OK";
        doc["hasConfig"] = false;
        doc["mode"] = "auto";
        doc["server"] = "";
        doc["port"] = 1883;
        doc["username"] = "";
        doc["password"] = "";
    }
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebService::handleMQTTConfigSaveRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    String jsonStr = String((char*)data).substring(0, len);
    LOG_D("Save MQTT Config: %s", jsonStr.c_str());
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    
    if (error) {
        LOG_E("deserializeJson() failed: %s", error.c_str());
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid JSON\"}");
        return;
    }
    
    const char* modeStr = doc["mode"] | "auto";
    const char* server = doc["server"] | "";
    uint16_t port = doc["port"] | 1883;
    const char* username = doc["username"] | "";
    const char* password = doc["password"] | "";
    
    // 转换mode字符串为枚举
    MQTTMode mode = (strcmp(modeStr, "manual") == 0) ? MQTTMode::MANUAL : MQTTMode::AUTO;
    
    // 验证：如果是手动模式，服务器地址不能为空
    if (mode == MQTTMode::MANUAL && strlen(server) == 0) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Manual mode requires server address\"}");
        return;
    }
    
    // 保存MQTT配置
    if (dataStore.SaveMQTTConfig(mode, server, port, username, password)) {
        request->send(200, "application/json", "{\"result\":\"OK\",\"message\":\"MQTT config saved successfully\"}");
    } else {
        request->send(500, "application/json", "{\"result\":\"failed\",\"message\":\"Failed to save MQTT config\"}");
    }
}

void WebService::handleMQTTConfigClearRequest(AsyncWebServerRequest *request)
{
    // 先清除保存的配置，再停止重连。
    dataStore.ClearMQTTConfig();
    haManager.disconnect();
    
    request->send(200, "application/json", "{\"result\":\"OK\",\"message\":\"MQTT config cleared\"}");
}

void WebService::handleMQTTConnectRequest(AsyncWebServerRequest *request)
{
    // 检查WiFi连接状态
    if (!pWiFiManager || !pWiFiManager->isConnected()) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"WiFi not connected\"}");
        return;
    }
    
    // 自动发现并连接到MQTT服务器
    if (haManager.connectAuto()) {
        request->send(200, "application/json", "{\"result\":\"OK\",\"message\":\"Auto-discovered and connected to MQTT broker\",\"connected\":true}");
    } else {
        request->send(500, "application/json", "{\"result\":\"failed\",\"message\":\"Failed to discover or connect to MQTT broker\",\"connected\":false}");
    }
}

void WebService::handleMQTTStatusRequest(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    
    doc["connected"] = haManager.isConnected();
    doc["hasConfig"] = haManager.hasSavedConfig();
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// ==================== 备份管理接口实现（TF卡） ====================

// 列出TF卡中的备份文件
void WebService::handleBackupListRequest(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    JsonArray files = doc["files"].to<JsonArray>();

    std::vector<String> list = BackupManager::listBackups();
    for (size_t i = 0; i < list.size(); i++) {
        files.add(list[i]);
    }

    doc["result"] = "OK";
    doc["count"] = list.size();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// 导出全部备份到TF卡
void WebService::handleBackupExportRequest(AsyncWebServerRequest* request)
{
    String outPath, outError;
    bool ok = BackupManager::exportBackup(outPath, outError);

    JsonDocument doc;
    doc["result"] = ok ? "OK" : "failed";
    doc["message"] = ok ? "导出成功" : outError;
    doc["file"] = outPath;

    String output;
    serializeJson(doc, output);
    request->send(ok ? 200 : 500, "application/json", output);
}

// 从TF卡备份文件恢复
// 请求体: {"file":"MYNOVA_BK_xxx.mynova","signals":true,"system":true}
void WebService::handleBackupRestoreRequest(AsyncWebServerRequest* request, JsonVariant& doc)
{
    if (!doc.is<JsonObject>() ||
        (doc.containsKey("signals") && !doc["signals"].is<bool>()) ||
        (doc.containsKey("system") && !doc["system"].is<bool>())) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid restore options\"}");
        return;
    }
    String file = doc["file"].as<String>();
    if (file.length() == 0) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Missing file\"}");
        return;
    }
    bool restoreSignals = doc["signals"] | true;
    bool restoreSystem = doc["system"] | true;

    String message;
    bool ok = BackupManager::importBackup(file, restoreSignals, restoreSystem, message);

    JsonDocument resp;
    resp["result"] = ok ? "OK" : "failed";
    resp["message"] = message;

    String output;
    serializeJson(resp, output);
    request->send(ok ? 200 : 500, "application/json", output);
}

// 删除TF卡备份文件
// 请求体: {"file":"MYNOVA_BK_xxx.mynova"}
void WebService::handleBackupDeleteRequest(AsyncWebServerRequest* request, JsonVariant& doc)
{
    String file = doc["file"].as<String>();
    if (file.length() == 0) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Missing file\"}");
        return;
    }

    bool ok = BackupManager::deleteBackup(file);
    request->send(ok ? 200 : 500, "application/json",
                  ok ? "{\"result\":\"OK\"}" : "{\"result\":\"failed\",\"message\":\"删除失败\"}");
}

// ==================== 日志级别接口(规划 §六: 远程排障开关) ====================
// GET /api/loglevel              → {"level":3,"name":"I"}
// GET /api/loglevel?level=0..4   → 设置并返回新级别(NVS持久化, 重启保持)
void WebService::handleLogLevelRequest(AsyncWebServerRequest *request)
{
    // 带 level 参数时执行设置(0=关 1=错 2=警 3=信息 4=调试)
    if (request->hasParam("level")) {
        int lv = request->getParam("level")->value().toInt();
        if (lv < 0 || lv > 4) {
            request->send(400, "application/json",
                          "{\"result\":\"failed\",\"message\":\"level out of range (0-4)\"}");
            return;
        }
        Log::setLevel((LogLevel)lv);
        LOG_I("日志级别切换为%s (Web)", Log::levelName((LogLevel)lv));
    }

    JsonDocument doc;
    doc["level"] = (int)Log::getLevel();
    doc["name"] = Log::levelName(Log::getLevel());
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

// ==================== 设备状态/系统设置接口实现 ====================

// GET /api/status —— Web仪表盘数据源（版本/序列号/电池/内存/运行时长/信号统计/网络状态）
void WebService::handleStatusRequest(AsyncWebServerRequest *request)
{
    systemSetting.resetIdleTimer();
    SystemConfig config = systemSetting.getConfig();

    JsonDocument doc;
    doc["result"] = "OK";
    doc["model"] = "MYNOVA RFC V2";
    doc["version"] = FIRMWARE_VERSION;
    doc["buildDate"] = FIRMWARE_BUILD_DATE;

    // 设备序列号（与设备端VersionPage一致：EfuseMAC十六进制大写）
    char serial[16];
    snprintf(serial, sizeof(serial), "%08X%08X",
             (uint32_t)(ESP.getEfuseMac() >> 32), (uint32_t)ESP.getEfuseMac());
    doc["serial"] = serial;

    // 电池（读取最近一次采样缓存，不触发ADC）
    float voltage = batteryManager.getLastVoltage();
    doc["batteryVoltage"] = roundf(voltage * 100) / 100.0f;
    // 电量取整显示（浮点映射误差会产生 95.00000764 这类值）
    doc["batteryPercent"] = (int)roundf(batteryManager.calculateBatteryPercent(voltage));

    // 双堆内存水位
    HeapStat heap = HeapMonitor::getStats();
    doc["heapFree"] = heap.ramFree;
    doc["heapMinFree"] = heap.ramMinFree;
    doc["psramFree"] = heap.psFree;
    doc["psramMinFree"] = heap.psMinFree;

    doc["uptimeSec"] = (uint32_t)(millis() / 1000);

    // 信号与主页统计
    doc["rfCount"] = dataStore.GetSignalCount(SIG_RF);
    doc["irCount"] = dataStore.GetSignalCount(SIG_IR);
    doc["homeCount"] = dataStore.GetHomeProfileCount();

    // WiFi STA状态
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    bool connected = (WiFi.status() == WL_CONNECTED);
    wifi["enabled"] = config.WifiEnabled;
    wifi["connected"] = connected;
    if (connected) {
        wifi["ssid"] = WiFi.SSID();
        wifi["ip"] = WiFi.localIP().toString();
        wifi["rssi"] = WiFi.RSSI();
    } else {
        wifi["ssid"] = config.WifiName;
    }

    // AP热点状态
    JsonObject ap = doc["ap"].to<JsonObject>();
    ap["enabled"] = config.APEnabled;
    ap["ssid"] = config.APName;
    if (WiFi.getMode() & WIFI_AP) {
        ap["ip"] = WiFi.softAPIP().toString();
    }

    // MQTT/HA连接状态
    JsonObject mqtt = doc["mqtt"].to<JsonObject>();
    mqtt["connected"] = haManager.isConnected();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// GET /api/settings —— 返回当前系统设置
void WebService::handleSettingsGetRequest(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    doc["result"] = "OK";
    doc["brightness"] = systemSetting.getBrightness();
    doc["soundAlertEnable"] = systemSetting.getSoundAlertEnable();
    doc["repeatTransmit"] = systemSetting.getRepeatTransmit();
    doc["txPowerLevel"] = systemSetting.getTxPowerLevel();
    doc["rxSensitivity"] = systemSetting.getRxSensitivity();
    doc["autoSleepTime"] = systemSetting.getAutoSleepTime();
    doc["autoScreenOffTime"] = systemSetting.getAutoScreenOffTime();
    doc["bootAnim"] = systemSetting.getBootAnim();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/settings —— 部分字段可选更新（只处理body中出现的键，立即生效并持久化NVS）
void WebService::handleSettingsPostRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    systemSetting.resetIdleTimer();
    String jsonStr = String((char*)data).substring(0, len);
    LOG_D("Settings Update: %s", jsonStr.c_str());

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"JSON parsing failed\"}");
        return;
    }

    // 逐键校验并应用（范围与设备端设置页一致）
    if (!doc["brightness"].isNull()) {
        int v = doc["brightness"].as<int>();
        if (v < 0 || v > 100) { request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"brightness out of range (0-100)\"}"); return; }
        systemSetting.setBrightness(v, true);
    }
    if (!doc["soundAlertEnable"].isNull()) {
        systemSetting.setSoundAlertEnable(doc["soundAlertEnable"].as<bool>(), true);
    }
    if (!doc["repeatTransmit"].isNull()) {
        int v = doc["repeatTransmit"].as<int>();
        if (v < 1 || v > 50) { request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"repeatTransmit out of range (1-50)\"}"); return; }
        systemSetting.setRepeatTransmit(v, true);
    }
    if (!doc["txPowerLevel"].isNull()) {
        int v = doc["txPowerLevel"].as<int>();
        if (v < 0 || v > 7) { request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"txPowerLevel out of range (0-7)\"}"); return; }
        systemSetting.setTxPowerLevel(v, true);
    }
    if (!doc["rxSensitivity"].isNull()) {
        int v = doc["rxSensitivity"].as<int>();
        if (v < 0 || v > 2) { request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"rxSensitivity out of range (0-2)\"}"); return; }
        systemSetting.setRxSensitivity(v, true);
    }
    if (!doc["autoSleepTime"].isNull()) {
        long v = doc["autoSleepTime"].as<long>();
        // 0=关闭；其余允许10秒~60分钟（Web端提供与设备页一致的档位）
        if (v != 0 && (v < 10000L || v > 3600000L)) { request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"autoSleepTime invalid\"}"); return; }
        systemSetting.setAutoSleepTime(v, true);
    }
    if (!doc["autoScreenOffTime"].isNull()) {
        long v = doc["autoScreenOffTime"].as<long>();
        if (v != 0 && (v < 10000L || v > 3600000L)) { request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"autoScreenOffTime invalid\"}"); return; }
        systemSetting.setAutoScreenOffTime(v, true);
    }
    if (!doc["bootAnim"].isNull()) {
        int v = doc["bootAnim"].as<int>();
        if (v < 0 || v > 5) { request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"bootAnim out of range (0-5)\"}"); return; }
        systemSetting.setBootAnim(v, true);
    }

    request->send(200, "application/json", "{\"result\":\"OK\",\"message\":\"Settings saved\"}");
}

// POST /api/reboot —— 立即响应后延迟重启
void WebService::handleRebootRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    request->send(200, "application/json", "{\"result\":\"OK\",\"message\":\"Rebooting\"}");
    LOG_I("Web触发重启");
    scheduleRestart(800);
}

// POST /api/factoryreset {"confirm":"YES"} —— 对齐设备端FactoryResetPage输入确认模式
void WebService::handleFactoryResetRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    String jsonStr = String((char*)data).substring(0, len);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"JSON parsing failed\"}");
        return;
    }

    String confirm = doc["confirm"].as<String>();
    if (confirm != "YES") {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Confirmation required: send {\\\"confirm\\\":\\\"YES\\\"}\"}");
        return;
    }

    LOG_W("Web触发恢复出厂！");
    request->send(200, "application/json", "{\"result\":\"OK\",\"message\":\"Factory reset, rebooting\"}");
    xTaskCreatePinnedToCore(factoryResetTask, "web_freset", 4096, nullptr, 5, nullptr, tskNO_AFFINITY);
}

// ==================== RF接收捕获接口实现（Web嗅探工作流） ====================

// POST /api/rf/rx/start —— {"frequency": 433.92} 单频点守候 或 {"band": "low"|"high"} 频段扫描
void WebService::handleRfRxStartRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    systemSetting.resetIdleTimer();
    String jsonStr = String((char*)data).substring(0, len);
    LOG_D("RF rx start: %s", jsonStr.c_str());

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"JSON parsing failed\"}");
        return;
    }

    // 设备端接收页占用时拒绝（所有权互斥）
    if (radioHelper.IsReceiving() && !webOwnedRx) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Device is busy\"}");
        return;
    }

    bool useFreq = !doc["frequency"].isNull();
    int band = -1;
    if (!doc["band"].isNull()) {
        String b = doc["band"].as<String>();
        if (b == "low") band = RF_BAND_LOW;
        else if (b == "high") band = RF_BAND_HIGH;
        else { request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid band (low/high)\"}"); return; }
    }
    if (!useFreq && band < 0) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"frequency or band required\"}");
        return;
    }

    float freq = useFreq ? doc["frequency"].as<float>() : 0.0f;
    if (useFreq && (freq < 300.0f || freq > 460.0f)) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"frequency out of range (300-460MHz)\"}");
        return;
    }

    hasCapturedRf = false;
    if (useFreq) radioHelper.EnableReceiveFixed(freq);
    else radioHelper.EnableReceive((RFBand)band);

    // CC1101未就绪时Enable*不会进入接收模式
    if (!radioHelper.IsReceiving()) {
        webOwnedRx = false;
        request->send(500, "application/json", "{\"result\":\"failed\",\"message\":\"CC1101 not ready\"}");
        return;
    }
    webOwnedRx = true;

    request->send(200, "application/json", "{\"result\":\"OK\",\"receiving\":true}");
}

// GET /api/rf/rx/status —— 接收中返回当前频点；捕获到新信号后返回解码结果
void WebService::handleRfRxStatusRequest(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    doc["result"] = "OK";
    bool receiving = radioHelper.IsReceiving();
    doc["receiving"] = receiving;
    doc["owned"] = webOwnedRx;

    // 仅消费Web自己启动的接收会话产生的信号（不窃取设备页面的接收结果）
    if (webOwnedRx && radioHelper.hasNewSignal) {
        // 接收任务置hasNewSignal后即停止改写，此时读取安全（见RadioHelper.h跨核规约）
        lastCapturedRf = radioHelper.rcData;
        hasCapturedRf = true;
        radioHelper.ClearNewSignal();
    }

    if (receiving) {
        doc["scanFrequency"] = radioHelper.GetCurrentScanFrequency();
    }

    doc["hasSignal"] = hasCapturedRf;
    if (hasCapturedRf) {
        doc["frequency"] = lastCapturedRf.frequency;
        doc["protocolName"] = lastCapturedRf.protocolName;
        doc["bitCount"] = lastCapturedRf.bitCount;
        doc["shortPulse"] = lastCapturedRf.shortPulse;
        doc["longPulse"] = lastCapturedRf.longPulse;
        doc["syncPulse"] = lastCapturedRf.syncPulse;
        doc["dataHex"] = rfDataToHex(lastCapturedRf);
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/rf/rx/stop —— 停止Web发起的接收
void WebService::handleRfRxStopRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    if (webOwnedRx) radioHelper.DisableReceive();
    webOwnedRx = false;
    request->send(200, "application/json", "{\"result\":\"OK\"}");
}

// POST /api/rf/rx/save {"name":"..."} —— 将捕获草稿保存到首个空RF槽位
void WebService::handleRfRxSaveRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    systemSetting.resetIdleTimer();
    String jsonStr = String((char*)data).substring(0, len);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"JSON parsing failed\"}");
        return;
    }

    if (!hasCapturedRf) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"No captured signal\"}");
        return;
    }
    String name = doc["name"].as<String>();
    if (!validSignalName(name)) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid name (1-31 bytes)\"}");
        return;
    }

    int emptyIndex = -1;
    for (int i = 1; i <= RF_SLOT_COUNT; i++) {
        if (!dataStore.SignalExists(SIG_RF, i)) { emptyIndex = i; break; }
    }
    if (emptyIndex == -1) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"No empty slot available\"}");
        return;
    }

    SignalEntry entry;
    entry.kind = SIG_RF;
    entry.name = name;
    entry.rf = lastCapturedRf;
    dataStore.SaveSignal(emptyIndex, entry);

    LOG_I("Web捕获保存: %s -> RF槽位%d", name.c_str(), emptyIndex);
    hasCapturedRf = false;   // 草稿已消费

    JsonDocument resp;
    resp["result"] = "OK";
    resp["index"] = emptyIndex;
    String output;
    serializeJson(resp, output);
    request->send(200, "application/json", output);
}

// ==================== IR学习捕获接口实现 ====================

// POST /api/ir/learn/start —— 启动红外学习（与设备端IRLearnPage互斥）
void WebService::handleIrLearnStartRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    systemSetting.resetIdleTimer();

    if (irHelper.IsReceiving() && !webOwnedIr) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Device is busy\"}");
        return;
    }

    hasLastIrMeta = false;
    irHelper.EnableReceive();
    webOwnedIr = true;

    request->send(200, "application/json", "{\"result\":\"OK\",\"learning\":true}");
}

// GET /api/ir/learn/status —— 学习中实时返回已收帧数；完成后返回协议/位宽/码值/空调状态
void WebService::handleIrLearnStatusRequest(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    doc["result"] = "OK";
    doc["owned"] = webOwnedIr;

    bool learning = irHelper.IsReceiving();
    uint8_t frames = irHelper.GetReceivedFrameCount();
    doc["learning"] = learning;
    doc["frameCount"] = frames;

    // 静默判定完成（CheckLearnFinished内部会停止学习任务并提示音，与设备页轮询用法一致）
    if (!hasLastIrMeta && webOwnedIr && irHelper.CheckLearnFinished()) {
        lastIrMeta = irHelper.getMeta();
        if (ensureIrEdgesBuffer()) {
            memcpy(lastIrEdges, irHelper.getEdges(), lastIrMeta.totalEdges * sizeof(uint16_t));
            hasLastIrMeta = true;
        }
    }

    doc["finished"] = hasLastIrMeta;
    if (hasLastIrMeta) {
        doc["frameCount"] = lastIrMeta.frameCount;
        doc["protocolName"] = lastIrMeta.protocolName;
        doc["totalEdges"] = lastIrMeta.totalEdges;
        JsonArray frameBits = doc["frameBits"].to<JsonArray>();
        JsonArray frameValues = doc["frameValues"].to<JsonArray>();
        for (uint8_t i = 0; i < lastIrMeta.frameCount; i++) {
            frameBits.add(lastIrMeta.frameBits[i]);
            char valHex[24];
            snprintf(valHex, sizeof(valHex), "%llX", (unsigned long long)lastIrMeta.frameValue[i]);
            frameValues.add(String(valHex));
        }
        if (lastIrMeta.acStateDesc[0]) doc["acState"] = lastIrMeta.acStateDesc;
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/ir/learn/cancel —— 取消学习并丢弃草稿
void WebService::handleIrLearnCancelRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    if (webOwnedIr) irHelper.DisableReceive();
    webOwnedIr = false;
    hasLastIrMeta = false;
    request->send(200, "application/json", "{\"result\":\"OK\"}");
}

// POST /api/ir/learn/save {"name":"..."} —— 将学习结果保存到首个空IR槽位
void WebService::handleIrLearnSaveRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    systemSetting.resetIdleTimer();
    String jsonStr = String((char*)data).substring(0, len);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"JSON parsing failed\"}");
        return;
    }

    if (!hasLastIrMeta) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"No learned signal\"}");
        return;
    }
    String name = doc["name"].as<String>();
    if (!validSignalName(name)) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid name (1-31 bytes)\"}");
        return;
    }

    int emptyIndex = -1;
    for (int i = 1; i <= IR_SLOT_COUNT; i++) {
        if (!dataStore.SignalExists(SIG_IR, i)) { emptyIndex = i; break; }
    }
    if (emptyIndex == -1) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"No empty slot available\"}");
        return;
    }

    SignalEntry entry;
    entry.kind = SIG_IR;
    entry.name = name;
    entry.ir = lastIrMeta;
    dataStore.SaveSignal(emptyIndex, entry, lastIrEdges);

    LOG_I("Web学习保存: %s -> IR槽位%d", name.c_str(), emptyIndex);
    hasLastIrMeta = false;   // 草稿已消费

    JsonDocument resp;
    resp["result"] = "OK";
    resp["index"] = KindIndexToSlot(SIG_IR, emptyIndex);   // 统一槽位编码（101..200）
    String output;
    serializeJson(resp, output);
    request->send(200, "application/json", output);
}

// IR边沿副本缓冲懒分配（8KB，优先PSRAM）
bool WebService::ensureIrEdgesBuffer()
{
    if (lastIrEdges != nullptr) return true;
    lastIrEdges = (uint16_t*)ps_malloc(IR_MAX_FRAMES * IR_MAX_FRAME_EDGES * sizeof(uint16_t));
    if (lastIrEdges == nullptr) {
        lastIrEdges = (uint16_t*)malloc(IR_MAX_FRAMES * IR_MAX_FRAME_EDGES * sizeof(uint16_t));
    }
    return lastIrEdges != nullptr;
}

// ==================== 多主页管理接口实现 ====================

// GET /api/home/list —— 全部主页 + 当前激活id
void WebService::handleHomeListRequest(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    doc["result"] = "OK";

    std::vector<HomeProfile> list = dataStore.ListHomeProfiles();
    doc["count"] = list.size();
    doc["active"] = dataStore.GetActiveHomeProfile();
    JsonArray profiles = doc["profiles"].to<JsonArray>();
    for (size_t i = 0; i < list.size(); i++) {
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = list[i].id;
        p["name"] = list[i].name;
        p["layout"] = list[i].layout;
        JsonArray slots = p["slots"].to<JsonArray>();
        for (int k = 0; k < 9; k++) slots.add(list[i].slotMap[k]);
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/home/create {"name","layout"} —— 新建主页（layout 0..8 即1..9键）
void WebService::handleHomeCreateRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    systemSetting.resetIdleTimer();
    String jsonStr = String((char*)data).substring(0, len);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"JSON parsing failed\"}");
        return;
    }

    String name = doc["name"].as<String>();
    int layout = doc["layout"].as<int>();
    if (name.length() == 0 || name.length() > HOME_NAME_MAX) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid name (1-32 bytes)\"}");
        return;
    }
    if (layout < 0 || layout > 8) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"layout out of range (0-8)\"}");
        return;
    }

    int id = dataStore.CreateHomeProfile(name, (uint8_t)layout);
    if (id < 0) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Home profiles full (max 8)\"}");
        return;
    }

    JsonDocument resp;
    resp["result"] = "OK";
    resp["id"] = id;
    String output;
    serializeJson(resp, output);
    request->send(200, "application/json", output);
}

// POST /api/home/save {"id","name","layout","slots":[9]} —— 保存主页（布局与键位绑定）
void WebService::handleHomeSaveRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    systemSetting.resetIdleTimer();
    String jsonStr = String((char*)data).substring(0, len);
    LOG_D("Home save: %s", jsonStr.c_str());

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"JSON parsing failed\"}");
        return;
    }

    int id = doc["id"].as<int>();
    if (id < 0 || id >= HOME_PROFILE_MAX) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid id\"}");
        return;
    }
    HomeProfile profile;
    if (!dataStore.LoadHomeProfile(id, profile)) {
        request->send(404, "application/json", "{\"result\":\"failed\",\"message\":\"Home profile not found\"}");
        return;
    }

    if (!doc["name"].isNull()) {
        String name = doc["name"].as<String>();
        if (name.length() == 0 || name.length() > HOME_NAME_MAX) {
            request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid name (1-32 bytes)\"}");
            return;
        }
        profile.name = name;
    }
    if (!doc["layout"].isNull()) {
        int layout = doc["layout"].as<int>();
        if (layout < 0 || layout > 8) {
            request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"layout out of range (0-8)\"}");
            return;
        }
        profile.layout = (uint8_t)layout;
    }
    if (!doc["slots"].isNull() && doc["slots"].is<JsonArray>()) {
        JsonArray slots = doc["slots"].as<JsonArray>();
        if (slots.size() != 9) {
            request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"slots must have 9 items\"}");
            return;
        }
        for (int k = 0; k < 9; k++) {
            int v = slots[k].as<int>();
            if (v < 0 || v > SLOT_IR_OFFSET + IR_SLOT_COUNT) {
                request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid slot value\"}");
                return;
            }
            profile.slotMap[k] = (int16_t)v;
        }
    }

    if (!dataStore.SaveHomeProfile(profile)) {
        request->send(500, "application/json", "{\"result\":\"failed\",\"message\":\"Failed to save home profile\"}");
        return;
    }

    request->send(200, "application/json", "{\"result\":\"OK\"}");
}

// POST /api/home/delete {"id"}
void WebService::handleHomeDeleteRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    String jsonStr = String((char*)data).substring(0, len);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"JSON parsing failed\"}");
        return;
    }

    int id = doc["id"].as<int>();
    if (id < 0 || id >= HOME_PROFILE_MAX) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid id\"}");
        return;
    }

    // 注意：DataStore::DeleteHomeProfile内部处理"删除最后一个主页自动重建默认"逻辑
    if (!dataStore.DeleteHomeProfile(id)) {
        request->send(404, "application/json", "{\"result\":\"failed\",\"message\":\"Home profile not found\"}");
        return;
    }

    request->send(200, "application/json", "{\"result\":\"OK\"}");
}

// POST /api/home/activate {"id"} —— 切换设备当前激活主页（立即生效）
void WebService::handleHomeActivateRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    systemSetting.resetIdleTimer();
    String jsonStr = String((char*)data).substring(0, len);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"JSON parsing failed\"}");
        return;
    }

    int id = doc["id"].as<int>();
    if (id < 0 || id >= HOME_PROFILE_MAX) {
        request->send(400, "application/json", "{\"result\":\"failed\",\"message\":\"Invalid id\"}");
        return;
    }
    HomeProfile profile;
    if (!dataStore.LoadHomeProfile(id, profile)) {
        request->send(404, "application/json", "{\"result\":\"failed\",\"message\":\"Home profile not found\"}");
        return;
    }

    if (!dataStore.SetActiveHomeProfile(id)) {
        request->send(500, "application/json", "{\"result\":\"failed\",\"message\":\"Failed to activate home profile\"}");
        return;
    }
    request->send(200, "application/json", "{\"result\":\"OK\"}");
}

// ==================== OTA固件升级接口实现（浏览器上传） ====================

// GET /api/ota/info —— 当前运行分区与可写空间
void WebService::handleOTAInfoRequest(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    doc["result"] = "OK";
    const esp_partition_t* running = esp_ota_get_running_partition();
    doc["partition"] = running ? running->label : "app";
    doc["sketchSize"] = (uint32_t)ESP.getSketchSize();
    doc["freeSketchSpace"] = (uint32_t)ESP.getFreeSketchSpace();
    doc["version"] = FIRMWARE_VERSION;

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/ota/upload（multipart, 字段名firmware）—— 上传回调：分片写入OTA分区
// 写入app0/app1中未运行的那个分区，Update.end校验失败自动放弃（双分区保护）
void WebService::handleOTAUploadRequest(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final)
{
    if (index == 0) {
        LOG_I("OTA上传开始: %s", filename.c_str());
        otaUpdateOK = true;
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            LOG_E("OTA begin失败: %s", Update.errorString());
            otaUpdateOK = false;
        }
    }
    if (!otaUpdateOK) return;

    if (Update.write(data, len) != len) {
        LOG_E("OTA write失败: %s", Update.errorString());
        otaUpdateOK = false;
        Update.abort();
        return;
    }

    if (final) {
        if (Update.end(true)) {
            LOG_I("OTA校验成功, 共%u字节, 准备重启", (uint32_t)Update.size());
        } else {
            LOG_E("OTA end失败: %s", Update.errorString());
            otaUpdateOK = false;
        }
    }
}

// OTA上传完成（onRequest在upload结束后触发）：响应并延迟重启
void WebService::handleOTAUploadDoneRequest(AsyncWebServerRequest *request)
{
    if (otaUpdateOK) {
        request->send(200, "application/json", "{\"result\":\"OK\",\"message\":\"Firmware updated, rebooting\"}");
        scheduleRestart(800);
    } else {
        request->send(500, "application/json", "{\"result\":\"failed\",\"message\":\"Firmware update failed\"}");
    }
}

