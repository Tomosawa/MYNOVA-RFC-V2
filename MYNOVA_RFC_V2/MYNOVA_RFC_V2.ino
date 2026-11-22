/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

#include <Arduino.h>
#include "src/Bsp/IOPin.h"
#include "src/GUI/GUIRender.h"
#include "src/Drivers/ButtonHandle.h"
#include "src/Drivers/RadioHelper.h"
#include "src/Drivers/IRHelper.h"
#include "src/Drivers/BatteryManager.h"
#include "src/Services/SystemSetting.h"
#include "src/GUI/Widget/UITitleBar.h"
#include "src/GUI/UIEngine.h"
#include "src/Pages/Home/HomePage.h"
#include "Version.h"
#include "src/Services/WebService.h"
#include "src/Services/WiFiManager.h"
#include "src/Services/DataStore.h"
#include "src/Services/BackupManager.h"
#include "src/Services/OTAUpdater.h"
#include "src/Drivers/SplashScreen.h"
#include "src/Services/HAManager.h"
#include "src/Drivers/AudioPlayer.h"
#include "src/Services/TalkManager.h"
#include "src/Util/Log.h"
#include "src/Util/HeapMonitor.h"

DataStore dataStore;
GUIRender guiRender;
ButtonHandle buttonHandle;
RadioHelper radioHelper;
IRHelper irHelper;
BatteryManager batteryManager;
SystemSetting systemSetting;
WebService webService;
WiFiManager wifiManager;
SplashScreen splashScreen;
HAManager haManager;
extern HomePage uiPageHome;

void setup()
{
    Serial.begin(115200);
    Serial.println(">>>>>>>>>>MYNOVA RFC V2 Started!<<<<<<<<<<");

    if (!BackupManager::recoverInterruptedRestore()) {
        Serial.println("Cannot replay backup rollback journal; startup halted");
        for (;;) delay(1000);
    }

    // 分级日志系统: 从NVS恢复级别, 必须最先就绪(后续模块日志才能被过滤/输出)
    Log::begin();
    OTAUpdater::configureTlsMemory();

    // 版本信息
    String version_info = "MYNOVA RFC Firmware V" + String(FIRMWARE_VERSION) + " \r\nBuild " + String(FIRMWARE_BUILD_DATE);
    Serial.println(version_info);
    // 获取设备ID
    Serial.println("Device ID: " + String(ESP.getEfuseMac(), HEX));

    // ========== 第一步：初始化显示屏并显示启动画面 ==========
    guiRender.initDisplay();
    splashScreen.init(guiRender.getU8G2());

    // ========== 初始化各模块，更新进度 ==========
    // 等待一律走 splashScreen.tick(): 动画模式在等待期间持续刷帧(否则动画只在
    // setProgress时渲染一帧, 呈幻灯片跳变); fastBoot(关闭动画)则完全跳过装饰等待

    // 初始化信号存储(NVS Preferences, 射频/红外统一槽位; LittleFS仅存Web前端)
    dataStore.begin();
    // 读取开机动画设置（需在 dataStore.begin 之后），关闭=极速开机
    int bootAnimStyle = dataStore.LoadSystemConfig().bootAnim;
    if (bootAnimStyle < 0 || bootAnimStyle > 5) bootAnimStyle = 1;
    splashScreen.setStyle(bootAnimStyle);
    bool fastBoot = (bootAnimStyle == 0);
    splashScreen.setProgress(5, "Data Store");
    if (!fastBoot) splashScreen.tick(50);

    // 初始化WiFi管理器 (10%): 含持久WiFiWorker任务(Core0)
    splashScreen.setProgress(10, "WiFi Manager");
    wifiManager.init();
    if (!fastBoot) splashScreen.tick(50);

    // 初始化Home Assistant集成 (25%)
    splashScreen.setProgress(25, "Home Assistant");
    haManager.init(&dataStore, &radioHelper, &irHelper);
    if (!fastBoot) splashScreen.tick(50);

    // 初始化系统设置 (40%)
    splashScreen.setProgress(40, "System Settings");
    systemSetting.init(&wifiManager);
    if (!fastBoot) splashScreen.tick(50);

    // 初始化Web服务 (55%)
    splashScreen.setProgress(55, "Web Service");
    webService.init(&wifiManager);
    webService.processRequest();
    if (!fastBoot) splashScreen.tick(50);

    // 初始化按键处理 (70%): 只建队列+注册中断, 分发由UIKernel轮询(不再建独立任务)
    splashScreen.setProgress(70, "Button Handler");
    buttonHandle.init();
    if (!fastBoot) splashScreen.tick(50);

    // 初始化电池管理 (85%)
    splashScreen.setProgress(85, "Battery Monitor");
    batteryManager.init();
    haManager.setBatteryManager(&batteryManager);
    if (!fastBoot) splashScreen.tick(50);

    // 初始化音频模块 (87%): WM8960提示音(原蜂鸣器替代)+音乐播放
    // 提示音不依赖TF卡, 开机即初始化保证射频/红外事件滴声可用
    splashScreen.setProgress(87, "Audio Module");
    audioPlayer.init();
    if (!fastBoot) splashScreen.tick(50);

    // 初始化射频模块：CC1101多频段 (90%)
    splashScreen.setProgress(90, "RF Module");
    radioHelper.init();
    if (!fastBoot) splashScreen.tick(50);

    // 初始化红外模块：学习/发射 (95%)
    splashScreen.setProgress(95, "IR Module");
    irHelper.init();
    if (!fastBoot) splashScreen.tick(50);

    // 初始化对讲通讯(96%): 只建任务/读NVS, 不开射频(进对讲页才开STA+ESP-NOW)
    splashScreen.setProgress(96, "Talk Module");
    talkManager.init();
    if (!fastBoot) splashScreen.tick(50);

    // 完成初始化 (100%)
    splashScreen.setProgress(100, "Ready!");
    if (!fastBoot) splashScreen.tick(300);

    // 播放完成动画
    splashScreen.finish();

    // ========== 启动主界面 ==========
    Serial.println("Setup: 启动UIKernel(主界面)");
    guiRender.init();  // 启动UIKernel(唯一UI任务: 输入分发+导航+渲染)

    // 双堆与任务栈水位: 启动横幅+任务表(核对双核落位)
    HeapMonitor::begin();

    Serial.println("==========MYNOVA RFC V2 Initialized!==========");
}

void loop()
{
  yield();

  // 更新系统设置（处理自动休眠和息屏）
  systemSetting.update();

  // 处理Home Assistant MQTT消息
  haManager.loop();

  // MQTT建连门禁（worker只置旗，耗时建连放loop慢路径，不堵WiFiWorker队列）：
  // STA实连 + NVS有MQTT配置才连；无配置不连、无WiFi连不上（connectAuto内部也会再判）。
  if (wifiManager.consumeMqttPending()) {
    if (wifiManager.isConnected() && haManager.hasSavedConfig()) {
      LOG_I("HA: STA已连且存在MQTT配置，开始连接");
      if (!haManager.connectAuto()) {
        LOG_E("HA: Home Assistant 连接失败，请通过Web界面检查配置");
      }
    } else {
      LOG_I("HA: 跳过MQTT连接（STA未连或无保存配置）");
    }
  }

  // 根据网络和监听状态同步 Web 服务，未能启动时稍后重试
  webService.processRequest();

  // 双堆水位周期上报(内部30秒节流)
  HeapMonitor::tick();

  // 串口运行期日志调级: 发送 "LOG:0"~"LOG:4" (0=关 1=错 2=警 3=信息 4=调试)
  // 串口调试进接收模式: 发送 "RF:RX:<频率>" (如 RF:RX:433.92), 无需按键
  if (Serial.available()) {
      String line = Serial.readStringUntil('\n');
      line.trim();
      if (line.startsWith("LOG:")) {
          int lv = line.substring(4).toInt();
          if (lv >= 0 && lv <= 4) {
              Log::setLevel((LogLevel)lv);
              Serial.printf("[I][LOG] level -> %s\n", Log::levelName((LogLevel)lv));
          }
      } else if (line.startsWith("RF:RX:")) {
          float freq = line.substring(6).toFloat();
          if (freq >= 300.0f && freq <= 460.0f) {
              guiRender.requestPowerSave(false);
              guiRender.requestRfReceive(freq);
              ButtonHandle::inject(BTN_WAKE);   // 息屏时唤醒UIKernel消费上面的请求
              Serial.printf("[I][RF] 串口命令: 进入 %.2fMHz 接收模式\n", freq);
          } else {
              Serial.println("[E][RF] 频率超出范围(300-460MHz)");
          }
      } else if (line.startsWith("RF:ANT")) {
          // 串口调试: 直接驱动天线开关并回读实际电平 ("RF:ANT:H"/"RF:ANT:L"/"RF:ANT:?")
          String arg = line.substring(6);
          pinMode(PIN_RF_SW_CTRL, OUTPUT);
          if (arg.equalsIgnoreCase(":H")) digitalWrite(PIN_RF_SW_CTRL, HIGH);
          else if (arg.equalsIgnoreCase(":L")) digitalWrite(PIN_RF_SW_CTRL, LOW);
          Serial.printf("[I][RF] ANT GPIO%d=%d\n", PIN_RF_SW_CTRL, digitalRead(PIN_RF_SW_CTRL));
      }
  }

  delay(10);
}
