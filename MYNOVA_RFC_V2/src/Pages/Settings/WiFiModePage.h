/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

#ifndef WiFiModePage_h
#define WiFiModePage_h

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UILabel.h"
#include "../../GUI/Widget/UISelectValue.h"
#include "../../GUI/Widget/UIInput.h"
#include "../../GUI/Widget/UIMenu.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../GUI/GUIRender.h"
#include "../../Services/WiFiManager.h"
#include <WiFi.h>
#include <vector>

// WiFi设置页: 支持设备端直接扫描选择SSID + T9输入密码连接,
// 与Web配置页共用SystemSetting/DataStore存储, 双向同步展示
class WiFiModePage : public UIPage {
public:
    WiFiModePage();

    // 重写按钮事件处理
    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButton1(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton3(void* context = nullptr) override;
    void onButton4(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton6(void* context = nullptr) override;
    void onButton7(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;
    void onButton9(void* context = nullptr) override;

    // 重写update方法，用于定时刷新WiFi状态/扫描结果
    void update() override;
    void showPage() override;

private:
    enum PageState {
        WSTATE_VIEW,       // 查看配置(SSID/密码/开关/连接状态)
        WSTATE_SCAN,       // SSID扫描列表
        WSTATE_INPUT_PWD   // T9密码输入
    };

    void initLayout();            // 初始化页面布局
    void updateDisplay();         // 更新显示
    void applyVisibility();       // 按页面状态切换控件可见性
    void refreshWiFiInfo();       // 刷新WiFi连接信息(含连接状态文案)
    void toggleWiFi();            // 切换WiFi开关
    void saveSettings();          // 返回前保存
    void updateHomePageWiFiStatus(); // 更新主页WiFi状态
    void setRightButtonText();    // 按选中项切换右键提示

    void enterScanState(bool rescan);            // 进入扫描列表
    void refreshScanList();                      // 拉取扫描结果刷新菜单
    void pickScanItem(int menuIndex);            // 选中某个SSID
    void enterPasswordInput(const String& ssid, const String& prefill, bool fromScan); // 进入密码输入
    void confirmPassword();                      // 确认输入并连接
    void startConnect(const String& ssid, const String& password);      // 发起连接

    UILabel* ssidLabel;        // "SSID:"
    UISelectValue* ssidValue;  // SSID显示(可选中, 确定进入扫描)
    UILabel* pwdLabel;         // "密码:"
    UISelectValue* pwdValue;   // 密码显示(可选中, 确定进入T9输入)
    UILabel* statusLabel;      // "开关:"
    UISelectValue* statusValue; // 开启/关闭(可选中切换)
    UILabel* linkLabel;        // "连接:"
    UILabel* linkValue;        // 连接状态/失败/IP展示(只读)
    UINavBar* navBar;

    UIMenu* scanMenu;          // SSID扫描列表
    UIInput* pwdInput;         // T9密码输入框
    UINavBar* inputNavBar;     // 输入状态导航栏

    PageState state;               // 当前页面状态
    bool wifiEnabled;              // WiFi是否启用
    String wifiSSID;               // 配置的SSID
    String wifiPassword;           // 配置的密码
    String wifiIP;                 // 当前IP(已连接时)
    String linkText;               // 连接状态文案
    bool isConnected;              // 是否已连接
    int selectedItem;              // VIEW态选中项 (0=SSID 1=密码 2=开关)
    unsigned long lastUpdateTime;  // 上次轮询时间
    wl_status_t lastWiFiStatus;    // 上次WiFi状态
    bool lastConnecting;           // 上次"连接中"判定(用于触发刷新)
    unsigned long connectStartMs;  // 本页发起连接的时间戳(0=未发起)
    bool connectTimedOut;          // 是否已判定连接超时
    bool scanHandled;              // 本次扫描结果是否已消费
    unsigned long scanStartMs;     // 扫描开始时间(超时保护)
    String pendingSsid;            // 密码输入阶段的目标SSID
    bool pwdFromScan;              // 密码输入是否来自扫描列表(决定返回目标)
    std::vector<WiFiScanResult> scanItems; // 扫描结果缓存
};

#endif
