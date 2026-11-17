/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef UITitleBar_h
#define UITitleBar_h

#include "UIWidget.h"
#include <Arduino.h>
#include <U8g2lib.h>

class UITitleBar : public UIWidget {
public:
  UITitleBar();
  void render(U8G2* u8g2,int offsetX = 0, int offsetY = 0) override;
  void showSendAnime();
  void showIRSendAnime();
  void showReciveAnime();
  void setSoundState(int state);
  void showAP(bool bShow);
  void showWifi(bool bShow);
  // 新语义: enabled=开关已开, connected=已连接/已就绪
  // 开启但未连接时图标闪烁, 连上后常显, 关闭则隐藏
  void setAPIcon(bool enabled, bool connected);
  void setWifiIcon(bool enabled, bool connected);
public:
  int signalStrength;//0-4：一共五格
  int signalMode;//信号模式：0发送；1接收
  int soundState;//声音提示：0静音；1开启
  int batteryLevel;//0-5:一共6档5格显示

private:
  String label;
  int animeCount;
  bool sendAnime = false;
  bool irSendAnime = false;
  int irAnimeFrame = 0;
  bool reciveAnime = false;
  bool bShowAP = false;
  bool bShowWifi = false;
  bool bAPConnected = false;   // AP是否已就绪(启动成功); 就绪常显, 开启未就绪闪烁
  bool bWifiConnected = false; // STA是否已连接; 连接常显, 开启未连接闪烁
};

#endif
