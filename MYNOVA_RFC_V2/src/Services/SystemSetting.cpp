/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

// SystemSetting.cpp
#include "SystemSetting.h"
#include "DataStore.h"
#include "../GUI/GUIRender.h"
#include "../Bsp/IOPin.h"
#include "../Drivers/AudioPlayer.h"
#include "../Drivers/RadioHelper.h"
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <driver/gpio.h>
#include "../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "SYS"

extern DataStore dataStore;
extern GUIRender guiRender;
extern RadioHelper radioHelper;

SystemSetting::SystemSetting() {
    lastActivityTime = 0;
    isScreenOff = false;
    pWiFiManager = nullptr;
}

void SystemSetting::init(WiFiManager* wifiMgr) {
    LOG_I("SystemSetting: 初始化系统设置");
    
    // 保存WiFiManager指针
    pWiFiManager = wifiMgr;
    
    // 从存储读取配置
    config = dataStore.LoadSystemConfig();
    
    // 设置默认值（如果配置为空）
    if (config.APName.length() == 0) {
        config.APName = DEFAULT_AP_NAME;
    }
    if (config.APPassword.length() == 0) {
        config.APPassword = DEFAULT_AP_PASSWORD;
    }
    
    // 应用亮度配置
    applyBrightness();
    
    // WiFiManager已默认OFF，按需再开
    if (config.APEnabled && pWiFiManager) {
        pWiFiManager->startAPAsync(config.APName, config.APPassword);
    }
    if (config.WifiEnabled && config.WifiName.length() > 0 && pWiFiManager) {
        pWiFiManager->connectToWiFiAsync(config.WifiName, config.WifiPassword);
    }
    
    // 重置空闲计时器
    resetIdleTimer();
    
    LOG_I("SystemSetting: 初始化完成");
    LOG_I("  亮度: %d", config.brightness);
    LOG_I("  声音提示: %d", config.soundAlertEnable);
    LOG_I("  自动休眠时间: %ld", config.autoSleepTime);
    LOG_I("  自动息屏时间: %ld", config.autoScreenOffTime);
    LOG_I("  AP热点: %d", config.APEnabled);
    LOG_I("  WiFi: %d", config.WifiEnabled);
    LOG_I("  WiFi名称: %s", config.WifiName.c_str());
}

void SystemSetting::update() {
    // 检查自动休眠
    if (getAutoSleepTime() > 0) {
        checkAutoSleep();
    }
    
    // 检查自动息屏
    if (getAutoScreenOffTime() > 0) {
        checkAutoScreenOff();
    }
}

void SystemSetting::resetIdleTimer() {
    lastActivityTime = millis();
    
    // 如果屏幕已关闭，则打开屏幕
    if (isScreenOff) {
        screenOn();
    }
}

void SystemSetting::checkAutoSleep() {
    unsigned long currentTime = millis();
    unsigned long idleTime = currentTime - lastActivityTime;
    
    // 检查是否超时（需要考虑millis溢出）
    const long timeout = getAutoSleepTime();
    if (timeout > 0 && idleTime >= (unsigned long)timeout) {
        LOG_I("SystemSetting: 自动休眠时间到，进入休眠");
        enterSleep();
    }
}

void SystemSetting::checkAutoScreenOff() {
    // 如果屏幕已经关闭，不需要再次检查
    if (isScreenOff) {
        return;
    }
    
    unsigned long currentTime = millis();
    unsigned long idleTime = currentTime - lastActivityTime;
    
    // 检查是否超时
    const long timeout = getAutoScreenOffTime();
    if (timeout > 0 && idleTime >= (unsigned long)timeout) {
        LOG_I("SystemSetting: 自动息屏时间到，关闭屏幕");
        screenOff();
    }
}

void SystemSetting::enterSleep() {
    LOG_I("SystemSetting: 进入深度休眠模式（Deep Sleep）");

    // 停止音频并给WM8960断电: 深度休眠后I2S时钟消失, 芯片寄存器若保持
    // 使能(耳机/喇叭驱动+DAC)会在电池上持续耗电整夜。
    // stop()为异步命令, 有界等待音频任务处理完成(任务每loop必查命令, ≤2秒兜底)
    if (audioPlayer.getState() != PLAYER_IDLE) {
        LOG_I("SystemSetting: 停止音频播放(休眠前断电WM8960)");
        audioPlayer.stop();
        unsigned long t0 = millis();
        while (audioPlayer.getState() != PLAYER_IDLE && millis() - t0 < 2000) {
            delay(10);
        }
    }

    // 关闭屏幕
    screenOff();

    // 通过WiFiManager关闭WiFi和AP
    if (pWiFiManager) {
        pWiFiManager->shutdown();
    }

    LOG_I("SystemSetting: 配置RTC GPIO唤醒源");

    // ESP32-S3的RTC GPIO为 0-21，本机所有按键均落在该范围，全部支持唤醒
    // 按键为 INPUT_PULLUP，按下为低电平，任意一键低电平即唤醒（ANY_LOW）
    const int wakePins[] = {
        PIN_KEY_ENTER, PIN_KEY_MENU, PIN_KEY_BACK,
        PIN_KEY_1, PIN_KEY_2, PIN_KEY_3, PIN_KEY_4,
        PIN_KEY_5, PIN_KEY_6, PIN_KEY_7, PIN_KEY_8, PIN_KEY_9
    };
    const char* wakeNames[] = {
        "ENTER","MENU","BACK","1","2","3","4","5","6","7","8","9"
    };
    const int wakePinsCount = sizeof(wakePins) / sizeof(wakePins[0]);

    uint64_t wakeup_pin_mask = 0;
    int rtc_gpio_count = 0;

    for (int i = 0; i < wakePinsCount; i++) {
        int pin = wakePins[i];
        if (pin <= 21) {
            wakeup_pin_mask |= (1ULL << pin);
            rtc_gpio_pullup_en((gpio_num_t)pin);
            rtc_gpio_pulldown_dis((gpio_num_t)pin);
            rtc_gpio_hold_en((gpio_num_t)pin);
            rtc_gpio_count++;
            LOG_I("  KEY_%s (GPIO%d)", wakeNames[i], pin);
        } else {
            LOG_W("  跳过非RTC引脚 KEY_%s GPIO%d", wakeNames[i], pin);
        }
    }

    if (rtc_gpio_count > 0) {
        // 任意一键按下（低电平）即唤醒 —— 必须用 ANY_LOW，ALL_LOW 要求所有键同时按下才会唤醒
        esp_sleep_enable_ext1_wakeup_io(wakeup_pin_mask, ESP_EXT1_WAKEUP_ANY_LOW);
        LOG_I("SystemSetting: 已配置%d个RTC按键作为唤醒源（任意一键可唤醒）", rtc_gpio_count);
    } else {
        LOG_W("SystemSetting: 警告 - 没有RTC GPIO按键，使用定时器唤醒（60秒）");
        esp_sleep_enable_timer_wakeup(60 * 1000000ULL);
    }
    
    LOG_I("SystemSetting: 进入深度休眠...");
    delay(100);
    
    // 进入深度休眠
    // 注意：Deep Sleep会重启设备，从setup()重新开始执行
    esp_deep_sleep_start();
    
    // 这行代码永远不会执行
}

void SystemSetting::wakeUp() {
    LOG_I("SystemSetting: 从休眠唤醒");
    
    // 打开屏幕
    screenOn();
    
    // 重启AP（如果之前启用）- 通过WiFiManager
    const SystemConfig current = getConfig();
    if (current.APEnabled && pWiFiManager) {
        pWiFiManager->startAPAsync(current.APName, current.APPassword);
    }
    
    // 重置计时器
    resetIdleTimer();
}

void SystemSetting::screenOff() {
    if (isScreenOff) {
        return;
    }

    LOG_I("SystemSetting: 关闭屏幕");
    // 跨线程请求: OLED的I2C操作由UIKernel帧内执行(单线程化改造)
    guiRender.requestPowerSave(true);
    isScreenOff = true;
}

void SystemSetting::screenOn() {
    if (!isScreenOff) {
        return;
    }
    
    LOG_I("SystemSetting: 打开屏幕");
    guiRender.requestPowerSave(false);
    isScreenOff = false;
}

void SystemSetting::applyBrightness() {
    // 将亮度值（0-100）转换为对比度（0-255）
    int contrast = (getBrightness() * 255) / 100;
    // 跨线程请求: 对比度I2C写由UIKernel帧内执行(单线程化改造)
    guiRender.requestContrast(contrast);
}

// 获取和设置配置
SystemConfig SystemSetting::getConfig() {
    std::lock_guard lock(configMutex);
    return config;
}

void SystemSetting::setConfig(SystemConfig newConfig, bool saveToFlash) {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return;
    std::lock_guard lock(configMutex);
    config = newConfig;
    
    // 应用配置
    applyBrightness();
    
    // 处理AP - 通过WiFiManager
    if (pWiFiManager) {
        if (config.APEnabled && !pWiFiManager->isAPStarted()) {
            pWiFiManager->startAPAsync(config.APName, config.APPassword);
        } else if (!config.APEnabled && pWiFiManager->isAPStarted()) {
            pWiFiManager->stopAP();
        }
        
        // 处理WiFi - 通过WiFiManager
        if (config.WifiEnabled && !pWiFiManager->isConnected()) {
            pWiFiManager->connectToWiFiAsync(config.WifiName, config.WifiPassword);
        } else if (!config.WifiEnabled && pWiFiManager->isConnected()) {
            pWiFiManager->disconnect();
        }
    }
    
    if (saveToFlash) {
        saveConfig();
    }
}

void SystemSetting::reloadRestoredConfig() {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return;
    std::lock_guard lock(configMutex);
    config = dataStore.LoadSystemConfig();
    if (config.APName.isEmpty()) config.APName = DEFAULT_AP_NAME;
    if (config.APPassword.isEmpty()) config.APPassword = DEFAULT_AP_PASSWORD;
    applyBrightness();
    radioHelper.SetRepeatTransmit(getRepeatTransmit());
    radioHelper.SetTxPowerLevel(getTxPowerLevel());
    Log::reloadLevel();
    // Keep the active network connection until reboot so Web restore can send its response.
}

void SystemSetting::saveConfig() {
    LOG_I("SystemSetting: 保存配置到存储");
    dataStore.SaveSystemConfig(getConfig());
}

// 单独设置项的方法
void SystemSetting::setBrightness(int brightness, bool saveToFlash) {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return;
    std::lock_guard lock(configMutex);
    config.brightness = brightness;
    applyBrightness();
    
    if (saveToFlash) {
        saveConfig();
    }
}

void SystemSetting::setSoundAlertEnable(bool enable, bool saveToFlash) {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return;
    std::lock_guard lock(configMutex);
    config.soundAlertEnable = enable;
    
    if (saveToFlash) {
        saveConfig();
    }
}

void SystemSetting::setRepeatTransmit(int times, bool saveToFlash) {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return;
    std::lock_guard lock(configMutex);
    config.repeatTransmit = times;

    if (saveToFlash) {
        saveConfig();
    }
}

void SystemSetting::setTxPowerLevel(int level, bool saveToFlash) {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return;
    std::lock_guard lock(configMutex);
    if (level < 0) level = 0;
    if (level > 7) level = 7;
    config.txPowerLevel = level;

    if (saveToFlash) {
        saveConfig();
    }
}

void SystemSetting::setRxSensitivity(int level, bool saveToFlash) {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return;
    std::lock_guard lock(configMutex);
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    config.rxSensitivity = level;

    if (saveToFlash) {
        saveConfig();
    }
}

void SystemSetting::setAutoSleepTime(long timeMs, bool saveToFlash) {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return;
    std::lock_guard lock(configMutex);
    config.autoSleepTime = timeMs;
    
    if (saveToFlash) {
        saveConfig();
    }
}

void SystemSetting::setAutoScreenOffTime(long timeMs, bool saveToFlash) {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return;
    std::lock_guard lock(configMutex);
    config.autoScreenOffTime = timeMs;
    
    if (saveToFlash) {
        saveConfig();
    }
}

void SystemSetting::setAPEnabled(bool enabled, bool saveToFlash) {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return;
    std::lock_guard lock(configMutex);
    config.APEnabled = enabled;
    
    // 先保存配置，再启动异步任务，避免并发冲突
    if (saveToFlash) {
        saveConfig();
    }
    
    // 通过WiFiManager控制AP
    if (pWiFiManager) {
        if (config.APEnabled) {
            pWiFiManager->startAPAsync(config.APName, config.APPassword);
        } else {
            pWiFiManager->stopAP();
        }
    }
}

void SystemSetting::setWifiEnabled(bool enabled, bool saveToFlash) {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return;
    std::lock_guard lock(configMutex);
    config.WifiEnabled = enabled;
    
    // 先保存配置，再启动异步任务，避免并发冲突
    if (saveToFlash) {
        saveConfig();
    }
    
    // 通过WiFiManager控制WiFi
    // 注意: 页面侧startConnect已先调setWifiConfig(内部会重连),
    // 这里的isConnected仍是旧值, 必须再次触发连接, 否则新SSID不生效
    if (pWiFiManager) {
        if (config.WifiEnabled) {
            if (config.WifiName.length() > 0) {
                pWiFiManager->connectToWiFiAsync(config.WifiName, config.WifiPassword);
            }
        } else if (pWiFiManager->isConnected()) {
            pWiFiManager->disconnect();
        }
    }
}

void SystemSetting::setAPConfig(String name, String password, bool saveToFlash) {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return;
    std::lock_guard lock(configMutex);
    config.APName = name;
    config.APPassword = password;
    
    // 如果AP正在运行，重启以应用新配置
    if (config.APEnabled && pWiFiManager) {
        pWiFiManager->stopAP();
        pWiFiManager->startAPAsync(config.APName, config.APPassword);
    }
    
    if (saveToFlash) {
        saveConfig();
    }
}

void SystemSetting::setWifiConfig(String ssid, String password, bool saveToFlash) {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return;
    std::lock_guard lock(configMutex);
    config.WifiName = ssid;
    config.WifiPassword = password;

    // 如果WiFi已启用: 无论当前是否已连接, 都断开后按新配置重连
    // (旧逻辑仅在已连接时重连, 会导致"未连接+换SSID"时不生效)
    if (config.WifiEnabled && pWiFiManager) {
        pWiFiManager->disconnect();
        // 空SSID不做无意义连接(页面开启开关但未配网时会先进扫描)
        if (config.WifiName.length() > 0) {
            pWiFiManager->connectToWiFiAsync(config.WifiName, config.WifiPassword);
        }
    }

    if (saveToFlash) {
        saveConfig();
    }
}

// 获取单独的配置项
bool SystemSetting::getSoundAlertEnable() {
    std::lock_guard lock(configMutex);
    return config.soundAlertEnable;
}

int SystemSetting::getBrightness() {
    std::lock_guard lock(configMutex);
    return config.brightness;
}

int SystemSetting::getRepeatTransmit() {
    std::lock_guard lock(configMutex);
    return config.repeatTransmit;
}

int SystemSetting::getTxPowerLevel() {
    std::lock_guard lock(configMutex);
    if (config.txPowerLevel < 0 || config.txPowerLevel > 7) return 7;
    return config.txPowerLevel;
}

int SystemSetting::getRxSensitivity() {
    std::lock_guard lock(configMutex);
    if (config.rxSensitivity < 0 || config.rxSensitivity > 2) return 1;
    return config.rxSensitivity;
}

long SystemSetting::getAutoSleepTime() {
    std::lock_guard lock(configMutex);
    return config.autoSleepTime;
}

long SystemSetting::getAutoScreenOffTime() {
    std::lock_guard lock(configMutex);
    return config.autoScreenOffTime;
}

int SystemSetting::getBootAnim() {
    std::lock_guard lock(configMutex);
    if (config.bootAnim < 0 || config.bootAnim > 5) return 1;
    return config.bootAnim;
}

void SystemSetting::setBootAnim(int style, bool saveToFlash) {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return;
    std::lock_guard lock(configMutex);
    if (style < 0) style = 0;
    if (style > 5) style = 5;
    config.bootAnim = style;
    if (saveToFlash) saveConfig();
}

// MQTT/Home Assistant配置
void SystemSetting::setMQTTEnabled(bool enabled, bool saveToFlash) {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return;
    std::lock_guard lock(configMutex);
    config.MQTTEnabled = enabled;
    
    if (saveToFlash) {
        saveConfig();
    }
    
    LOG_I("SystemSetting: MQTT %s", enabled ? "启用" : "禁用");
}

void SystemSetting::setMQTTConfig(String server, int port, String username, String password, bool saveToFlash) {
    DataStore::StorageGuard storage(dataStore);
    if (!storage) return;
    std::lock_guard lock(configMutex);
    config.MQTTServer = server;
    config.MQTTPort = port;
    config.MQTTUsername = username;
    config.MQTTPassword = password;
    
    if (saveToFlash) {
        saveConfig();
    }
    
    LOG_I("SystemSetting: MQTT配置已更新");
    LOG_I("  服务器: %s", server.c_str());
    LOG_I("  端口: %d", port);
}
