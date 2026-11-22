/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#include "OTAUpdater.h"
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <mbedtls/platform.h>
#include <mbedtls/ssl.h>
#include "../Util/HeapMonitor.h"
#include "../Util/Log.h"
#include "../Util/MemAlloc.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "OTA"

namespace {
// The allocator hook is shared by mbedTLS. Only the task doing OTA gets
// PSRAM-first allocations. Other tasks keep the core's internal RAM policy.
thread_local bool otaTlsActive = false;

class OtaTlsMemoryScope {
public:
    OtaTlsMemoryScope() : previous(otaTlsActive) { otaTlsActive = true; }
    ~OtaTlsMemoryScope() { otaTlsActive = previous; }

private:
    const bool previous;
};

#if defined(CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC) && defined(MBEDTLS_PLATFORM_MEMORY) && !defined(MBEDTLS_PLATFORM_CALLOC_MACRO)
void* tlsCalloc(size_t count, size_t size) {
    if (otaTlsActive) {
        if (void* ptr = psramCalloc(count, size)) {
            return ptr;
        }
    }
    return heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
#endif

void logTlsAllocationFailure() {
    const HeapStat heap = HeapMonitor::getStats();
    LOG_W("TLS allocation failed: internal RAM free=%u largest=%u, PSRAM free=%u largest=%u",
          (unsigned)heap.ramFree, (unsigned)heap.ramMaxAlloc,
          (unsigned)heap.psFree, (unsigned)heap.psMaxAlloc);
}
} // namespace

void OTAUpdater::configureTlsMemory() {
#if defined(CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC) && defined(MBEDTLS_PLATFORM_MEMORY) && !defined(MBEDTLS_PLATFORM_CALLOC_MACRO)
    if (ESP.getPsramSize() > 0) {
        if (mbedtls_platform_set_calloc_free(tlsCalloc, memFree) == 0) {
            LOG_I("Online update TLS allocations prefer PSRAM");
        } else {
            LOG_W("Could not configure PSRAM for online update TLS");
        }
    }
#endif
}

OTAUpdater::OTAUpdater() {
    lastError = "";
}

bool OTAUpdater::checkUpdate(String url, OTAInfo& info) {
    if (WiFi.status() != WL_CONNECTED) {
        lastError = "WiFi未连接";
        return false;
    }

    const OtaTlsMemoryScope tlsMemory;
    WiFiClientSecure client;
    HTTPClient http;
    client.setInsecure(); // 忽略SSL证书验证，简化操作
    client.setHandshakeTimeout(10); // 秒
    http.setConnectTimeout(5000);  // 毫秒
    http.setTimeout(10000);        // 毫秒
    
    LOG_I("Checking update from: %s", url.c_str());

    // 配置跟随重定向
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    if (!http.begin(client, url)) {
        lastError = "连接初始化失败";
        LOG_W("Update manifest URL rejected by HTTP client");
        return false;
    }
    
    const unsigned long requestStarted = millis();
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        if (httpCode < 0) {
            lastError = "更新请求失败 (" + String(httpCode) + ")";
            char detail[128] = {};
            int clientError = client.lastError(detail, sizeof(detail));
            LOG_W("Update manifest GET failed after %lu ms (%d: %s), secure client error %d: %s",
                  millis() - requestStarted, httpCode, http.errorToString(httpCode).c_str(), clientError,
                  clientError < 0 ? detail : "no additional error reported");
            if (clientError == MBEDTLS_ERR_SSL_ALLOC_FAILED) {
                lastError = "TLS内存不足";
                logTlsAllocationFailure();
            }
        } else {
            lastError = "HTTP错误: " + String(httpCode);
            LOG_W("Update manifest server returned HTTP %d", httpCode);
        }
        http.end();
        return false;
    }
    
    LOG_I("Update manifest returned HTTP 200, reading response");
    String payload = http.getString();
    http.end();
    
    LOG_I("Update JSON: %s", payload.c_str());
    
    // 解析JSON
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        lastError = "JSON解析失败";
        LOG_E("%s", error.c_str());
        return false;
    }
    
    info.version = doc["version"].as<String>();
    info.firmwareUrl = doc["url"].as<String>();
    info.firmwareSize = doc["filesize"].as<size_t>();
    info.firmwareCrc = doc["crc32"].as<String>();
    info.description = doc["description"].as<String>();
    
    // 处理Spiffs
    if (doc.containsKey("spiffs")) {
        JsonObject spiffs = doc["spiffs"];
        info.hasSpiffs = true;
        info.spiffsUrl = spiffs["url"].as<String>();
        info.spiffsSize = spiffs["filesize"].as<size_t>();
        info.spiffsCrc = spiffs["crc32"].as<String>();
    } else {
        info.hasSpiffs = false;
    }
    
    // 去除URL中的空格（如果有）
    info.firmwareUrl.trim();
    if (info.hasSpiffs) info.spiffsUrl.trim();
    
    return true;
}

bool OTAUpdater::performUpdate(String url, size_t size, int type, ProgressCallback callback) {
    if (WiFi.status() != WL_CONNECTED) {
        lastError = "WiFi未连接";
        return false;
    }
    
    const OtaTlsMemoryScope tlsMemory;
    WiFiClientSecure client;
    HTTPClient http;
    client.setInsecure();
    client.setTimeout(10000); // 设置超时 10s
    
    LOG_I("Downloading update from: %s", url.c_str());
    
    // 配置跟随重定向
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    if (!http.begin(client, url)) {
        lastError = "连接失败";
        return false;
    }
    
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        lastError = "HTTP错误: " + String(httpCode);
        if (httpCode < 0) {
            char detail[128] = {};
            int clientError = client.lastError(detail, sizeof(detail));
            LOG_W("Update download GET failed (%d), secure client error %d: %s",
                  httpCode, clientError, clientError < 0 ? detail : "no additional error reported");
            if (clientError == MBEDTLS_ERR_SSL_ALLOC_FAILED) {
                lastError = "TLS内存不足";
                logTlsAllocationFailure();
            }
        }
        http.end();
        return false;
    }
    
    int len = http.getSize();
    // 如果服务器返回长度，优先使用服务器返回的长度，否则使用JSON中的长度
    if (len > 0) {
        size = len;
    }
    
    // 检查是否有足够的空间
    bool canBegin = Update.begin(size, type == 1 ? U_SPIFFS : U_FLASH);
    if (!canBegin) {
        lastError = "空间不足";
        http.end();
        return false;
    }
    
    WiFiClient* stream = http.getStreamPtr();
    size_t written = 0;
    size_t total = size;
    uint8_t buff[1024];
    
    while (http.connected() && (written < total)) {
        size_t available = stream->available();
        if (available) {
            int c = stream->readBytes(buff, ((available > sizeof(buff)) ? sizeof(buff) : available));
            size_t w = Update.write(buff, c);
            if (w > 0) {
                written += w;
                if (callback) {
                    if (!callback(written, total)) {
                        lastError = "用户取消更新";
                        http.end();
                        Update.end();
                        return false;
                    }
                }
            } else {
                // 写入失败
                break;
            }
        } else {
            // 没有数据，检查是否取消
            if (callback) {
                if (!callback(written, total)) {
                    lastError = "用户取消更新";
                    http.end();
                    Update.end();
                    return false;
                }
            }
            delay(10); // 稍微增加延迟，避免空转过快
        }
    }
    
    http.end();
    
    if (written != total) {
        Update.end();
        lastError = "下载不完整";
        return false;
    }
    
    if (!Update.end()) {
        lastError = "更新失败: " + String(Update.getError());
        return false;
    }
    
    return true;
}
