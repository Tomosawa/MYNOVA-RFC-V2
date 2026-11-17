/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef __WEBSERVICE__H__
#define __WEBSERVICE__H__
#include <Arduino.h>
#include "ESPAsyncWebServer.h"
#include <DNSServer.h>
#include <SPIFFS.h>
#include <LittleFS.h>

#include "WiFiManager.h"
#include "DataStore.h"

class WebService
{
public:
    WebService();
    void init(WiFiManager *wifiMgr);
    void serverStart();
    void serverStop();
    void processRequest();

    // WIFI相关接口
    void handleWIFIScanRequest(AsyncWebServerRequest *request);
    void handleWIFISaveRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
    void handleWIFIClearRequest(AsyncWebServerRequest *request);
    void handleWIFIConnectRequest(AsyncWebServerRequest *request);
    void handleWIFIEnableRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
    void handleAPWIFIInfoRequest(AsyncWebServerRequest *request);
    void handleAPSaveRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
    void handleAPEnableRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
    
    // 无线遥控数据管理接口
    void handleRadioDataListRequest(AsyncWebServerRequest *request);
    void handleRadioDataAddRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
    void handleRadioDataUpdateRequest(AsyncWebServerRequest* request, JsonVariant& body);
    void handleRadioDataDeleteRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
    void handleRadioDataGetRequest(AsyncWebServerRequest *request);
    void handleRadioDataSendRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
    
    // MQTT/HA配置接口
    void handleMQTTConfigGetRequest(AsyncWebServerRequest *request);
    void handleMQTTConfigSaveRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
    void handleMQTTConfigClearRequest(AsyncWebServerRequest *request);
    void handleMQTTConnectRequest(AsyncWebServerRequest *request);
    void handleMQTTStatusRequest(AsyncWebServerRequest *request);


    // 备份管理接口（TF卡）
    void handleBackupListRequest(AsyncWebServerRequest *request);               // 列出备份文件
    void handleBackupExportRequest(AsyncWebServerRequest* request);  // 导出备份到TF卡
    void handleBackupRestoreRequest(AsyncWebServerRequest* request, JsonVariant& body);  // 从TF卡恢复
    void handleBackupDeleteRequest(AsyncWebServerRequest* request, JsonVariant& body);  // 删除备份文件

    // 日志级别接口: GET /api/loglevel 查询; /api/loglevel?level=0..4 运行期设置(NVS持久化)
    void handleLogLevelRequest(AsyncWebServerRequest *request);

    // ==================== 设备状态/系统设置接口 ====================
    void handleStatusRequest(AsyncWebServerRequest *request);               // GET /api/status 仪表盘数据源
    void handleSettingsGetRequest(AsyncWebServerRequest *request);          // GET /api/settings
    void handleSettingsPostRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total); // POST /api/settings 部分字段更新
    void handleRebootRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);      // POST /api/reboot
    void handleFactoryResetRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total); // POST /api/factoryreset {"confirm":"YES"}

    // ==================== RF接收捕获接口(Web嗅探工作流) ====================
    void handleRfRxStartRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);  // POST /api/rf/rx/start {frequency|band}
    void handleRfRxStatusRequest(AsyncWebServerRequest *request);           // GET /api/rf/rx/status
    void handleRfRxStopRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);   // POST /api/rf/rx/stop
    void handleRfRxSaveRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);   // POST /api/rf/rx/save {name}

    // ==================== IR学习捕获接口 ====================
    void handleIrLearnStartRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);  // POST /api/ir/learn/start
    void handleIrLearnStatusRequest(AsyncWebServerRequest *request);         // GET /api/ir/learn/status
    void handleIrLearnCancelRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total); // POST /api/ir/learn/cancel
    void handleIrLearnSaveRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);  // POST /api/ir/learn/save {name}

    // ==================== 多主页管理接口 ====================
    void handleHomeListRequest(AsyncWebServerRequest *request);             // GET /api/home/list
    void handleHomeCreateRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);  // POST /api/home/create {name,layout}
    void handleHomeSaveRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);    // POST /api/home/save {id,name,layout,slots}
    void handleHomeDeleteRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);   // POST /api/home/delete {id}
    void handleHomeActivateRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);// POST /api/home/activate {id}

    // ==================== OTA固件升级接口(浏览器上传) ====================
    void handleOTAInfoRequest(AsyncWebServerRequest *request);              // GET /api/ota/info
    void handleOTAUploadRequest(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final); // 上传回调
    void handleOTAUploadDoneRequest(AsyncWebServerRequest *request);        // 上传完成响应+重启

private:
    AsyncWebServer server;
    WiFiManager *pWiFiManager;

    // ── Web捕获会话状态（RF/IR接收与设备OLED页面互斥的所有权标志） ──
    bool webOwnedRx;             // RF接收由Web启动
    bool webOwnedIr;             // IR学习由Web启动
    RFSignalData lastCapturedRf; // Web捕获的RF信号草稿
    bool hasCapturedRf;
    IRSignalMeta lastIrMeta;     // Web学习的IR信号草稿（元数据+边沿副本）
    uint16_t* lastIrEdges;
    bool hasLastIrMeta;
    bool otaUpdateOK;            // OTA上传流程状态

    bool serverRunning;          // server.begin() 是否已成功执行（防重复begin/崩溃）
    bool pendingStart;           // 射频OFF时推迟启动，待STA/AP就绪后由processRequest补启动
    bool routesRegistered;       // 路由只注册一次，重启只调begin

    bool ensureIrEdgesBuffer();  // 懒分配IR边沿副本缓冲(优先PSRAM)
};

#endif