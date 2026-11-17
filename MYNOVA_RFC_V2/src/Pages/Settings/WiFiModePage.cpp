/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// WiFiModePage.cpp - 设备端WiFi配网: 扫描选择SSID + T9输入密码 + 连接状态展示
// 与Web配网共用SystemSetting/DataStore(NVS RadioData命名空间), 双向同步
#include "WiFiModePage.h"
#include "../../GUI/UIEngine.h"
#include "../../Services/SystemSetting.h"
#include "../../GUI/GUIRender.h"
#include "../../Services/WiFiManager.h"
#include "../Home/HomePage.h"

extern UIEngine uiEngine;
extern SystemSetting systemSetting;
extern WiFiManager wifiManager;
extern HomePage uiPageHome;

#define WIFI_VIEW_ROWS 3          // VIEW态可选行数: 0=SSID 1=密码 2=开关
#define WIFI_SCAN_TIMEOUT_MS 12000
#define WIFI_CONNECT_TIMEOUT_MS 15000

static String truncateFor128(const String& s, size_t maxChars) {
    if (s.length() <= maxChars) return s;
    return s.substring(0, maxChars - 2) + "..";
}

WiFiModePage::WiFiModePage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT) {
    // 以SystemSetting内存配置为准(Web保存后会同步进该配置, 见WebService同步修复)
    SystemConfig config = systemSetting.getConfig();
    wifiEnabled = config.WifiEnabled;
    wifiSSID = config.WifiName;
    wifiPassword = config.WifiPassword;

    state = WSTATE_VIEW;
    selectedItem = 0;
    lastUpdateTime = 0;
    lastWiFiStatus = WiFi.status();
    lastConnecting = false;
    connectStartMs = 0;
    connectTimedOut = false;
    scanHandled = true;
    scanStartMs = 0;
    pendingSsid = "";
    pwdFromScan = false;

    initLayout();
    refreshWiFiInfo();
    updateDisplay();
}

void WiFiModePage::initLayout() {
    // 4行 x 12px: SSID / 密码 / 开关 / 连接(只读)
    ssidLabel = new UILabel();
    ssidLabel->x = 0; ssidLabel->y = 0;
    ssidLabel->width = 34; ssidLabel->height = 12;
    ssidLabel->label = "SSID:";
    ssidLabel->textAlign = LEFT; ssidLabel->verticalAlign = MIDDLE;
    ssidLabel->textFont = u8g2_font_wqy12_t_gb2312;
    addWidget(ssidLabel);

    ssidValue = new UISelectValue();
    ssidValue->x = 36; ssidValue->y = 0;
    ssidValue->width = 92; ssidValue->height = 12;
    ssidValue->bShowBorder = true;
    ssidValue->textFont = u8g2_font_wqy12_t_gb2312;
    addWidget(ssidValue);

    pwdLabel = new UILabel();
    pwdLabel->x = 0; pwdLabel->y = 13;
    pwdLabel->width = 34; pwdLabel->height = 12;
    pwdLabel->label = "密码:";
    pwdLabel->textAlign = LEFT; pwdLabel->verticalAlign = MIDDLE;
    pwdLabel->textFont = u8g2_font_wqy12_t_gb2312;
    addWidget(pwdLabel);

    pwdValue = new UISelectValue();
    pwdValue->x = 36; pwdValue->y = 13;
    pwdValue->width = 92; pwdValue->height = 12;
    pwdValue->bShowBorder = true;
    pwdValue->textFont = u8g2_font_wqy12_t_gb2312;
    addWidget(pwdValue);

    statusLabel = new UILabel();
    statusLabel->x = 0; statusLabel->y = 26;
    statusLabel->width = 34; statusLabel->height = 12;
    statusLabel->label = "开关:";
    statusLabel->textAlign = LEFT; statusLabel->verticalAlign = MIDDLE;
    statusLabel->textFont = u8g2_font_wqy12_t_gb2312;
    addWidget(statusLabel);

    statusValue = new UISelectValue();
    statusValue->x = 36; statusValue->y = 26;
    statusValue->width = 92; statusValue->height = 12;
    statusValue->bShowBorder = true;
    statusValue->textFont = u8g2_font_wqy12_t_gb2312;
    addWidget(statusValue);

    linkLabel = new UILabel();
    linkLabel->x = 0; linkLabel->y = 39;
    linkLabel->width = 34; linkLabel->height = 12;
    linkLabel->label = "连接:";
    linkLabel->textAlign = LEFT; linkLabel->verticalAlign = MIDDLE;
    linkLabel->textFont = u8g2_font_wqy12_t_gb2312;
    addWidget(linkLabel);

    linkValue = new UILabel();
    linkValue->x = 36; linkValue->y = 39;
    linkValue->width = 92; linkValue->height = 12;
    linkValue->textAlign = LEFT; linkValue->verticalAlign = MIDDLE;
    linkValue->textFont = u8g2_font_wqy12_t_gb2312;
    addWidget(linkValue);

    navBar = new UINavBar(0, SCREEN_HEIGHT - 12, SCREEN_WIDTH, 12);
    navBar->setLeftButtonText("返回");
    navBar->setRightButtonText("扫描");
    addWidget(navBar);

    // SSID扫描列表(全屏菜单)
    scanMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    scanMenu->titleText = "选择WiFi";
    scanMenu->bShowBtnTips = true;
    scanMenu->bShowBorder = false;
    scanMenu->bShowTitle = false;
    scanMenu->bVisible = false;
    scanMenu->getNavBar()->setLeftButtonText("返回");
    scanMenu->getNavBar()->setRightButtonText("选择");
    addWidget(scanMenu);

    // T9密码输入
    pwdInput = new UIInput(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT - 14);
    pwdInput->setTextAlign(INPUT_LEFT);
    pwdInput->setTitle("输入密码");
    pwdInput->bVisible = false;
    addWidget(pwdInput);

    inputNavBar = new UINavBar(0, SCREEN_HEIGHT - 12, SCREEN_WIDTH, 12);
    inputNavBar->setLeftButtonText("返回");
    inputNavBar->setMiddleButtonText("删除");
    inputNavBar->setRightButtonText("确认");
    inputNavBar->bVisible = false;
    addWidget(inputNavBar);

    applyVisibility();
}

void WiFiModePage::showPage() {
    UIPage::showPage();
    // 每次进入页面都从SystemSetting重载, 保证Web端修改后此处实时展示
    SystemConfig config = systemSetting.getConfig();
    wifiEnabled = config.WifiEnabled;
    wifiSSID = config.WifiName;
    wifiPassword = config.WifiPassword;
    state = WSTATE_VIEW;
    lastWiFiStatus = WiFi.status();
    lastConnecting = false;
    refreshWiFiInfo();
    applyVisibility();
    updateDisplay();
}

void WiFiModePage::applyVisibility() {
    bool isView = (state == WSTATE_VIEW);
    bool isScan = (state == WSTATE_SCAN);
    bool isInput = (state == WSTATE_INPUT_PWD);
    ssidLabel->bVisible = isView;
    ssidValue->bVisible = isView;
    pwdLabel->bVisible = isView;
    pwdValue->bVisible = isView;
    statusLabel->bVisible = isView;
    statusValue->bVisible = isView;
    linkLabel->bVisible = isView;
    linkValue->bVisible = isView;
    navBar->bVisible = isView;
    scanMenu->bVisible = isScan;
    pwdInput->bVisible = isInput;
    inputNavBar->bVisible = isInput;
}

void WiFiModePage::setRightButtonText() {
    if (state != WSTATE_VIEW) return;
    if (selectedItem == 0) navBar->setRightButtonText("扫描");
    else if (selectedItem == 1) navBar->setRightButtonText("修改");
    else navBar->setRightButtonText("切换");
}

void WiFiModePage::updateDisplay() {
    refreshWiFiInfo();

    if (wifiSSID.length() == 0) ssidValue->value = "未设置";
    else ssidValue->value = truncateFor128(wifiSSID, 14);
    ssidValue->bSelected = (state == WSTATE_VIEW && selectedItem == 0);

    if (wifiPassword.length() == 0) pwdValue->value = "未设置";
    else pwdValue->value = truncateFor128(wifiPassword, 14);
    pwdValue->bSelected = (state == WSTATE_VIEW && selectedItem == 1);

    statusValue->value = wifiEnabled ? "开启" : "关闭";
    statusValue->bSelected = (state == WSTATE_VIEW && selectedItem == 2);

    linkValue->label = linkText;
    setRightButtonText();
}

void WiFiModePage::refreshWiFiInfo() {
    wl_status_t st = WiFi.status();
    isConnected = (st == WL_CONNECTED);

    if (isConnected) {
        wifiIP = WiFi.localIP().toString();
        String cur = WiFi.SSID();
        if (cur.length() > 0) wifiSSID = cur;
        linkText = wifiIP;
        connectStartMs = 0;
        connectTimedOut = false;
        return;
    }

    if (!wifiEnabled) {
        linkText = "已关闭";
        wifiIP = "";
        return;
    }
    if (connectTimedOut) {
        linkText = "连接超时";
        wifiIP = "";
        return;
    }
    // 连接中超时判定
    if (connectStartMs > 0 && millis() - connectStartMs > WIFI_CONNECT_TIMEOUT_MS) {
        connectStartMs = 0;
        connectTimedOut = true;
        linkText = "连接超时";
        wifiIP = "";
        return;
    }
    switch (st) {
        case WL_CONNECT_FAILED:
            linkText = "密码错误";
            break;
        case WL_NO_SSID_AVAIL:
            linkText = "无此SSID";
            break;
        case WL_CONNECTION_LOST:
            linkText = "连接丢失";
            break;
        case WL_DISCONNECTED:
        case WL_IDLE_STATUS:
        case WL_SCAN_COMPLETED:
            linkText = (connectStartMs > 0) ? "连接中..." : "未连接";
            break;
        default:
            linkText = "连接中...";
            break;
    }
    wifiIP = "";
}

void WiFiModePage::updateHomePageWiFiStatus() {
    uiPageHome.updateStatus();
}

void WiFiModePage::toggleWiFi() {
    wifiEnabled = !wifiEnabled;
    connectTimedOut = false;
    if (wifiEnabled) {
        // 开启但尚未配网 -> 直接进扫描, 不做无效连接
        if (wifiSSID.length() == 0) {
            systemSetting.setWifiEnabled(true, true);
            updateHomePageWiFiStatus();
            updateDisplay();
            enterScanState(true);
            return;
        }
        systemSetting.setWifiEnabled(true, true);
        // setWifiEnabled内部已按配置发起异步连接, 这里只做超时计时
        if (!wifiManager.isConnected()) connectStartMs = millis();
    } else {
        connectStartMs = 0;
        systemSetting.setWifiEnabled(false, true);
    }
    updateHomePageWiFiStatus();
    updateDisplay();
}

void WiFiModePage::saveSettings() {
    updateHomePageWiFiStatus();
}

void WiFiModePage::startConnect(const String& ssid, const String& password) {
    wifiSSID = ssid;
    wifiPassword = password;
    wifiEnabled = true;
    connectTimedOut = false;
    // 先存SSID/密码, 再开开关(内部会按新配置发起异步连接)
    systemSetting.setWifiConfig(ssid, password, true);
    systemSetting.setWifiEnabled(true, true);
    connectStartMs = millis();
    updateHomePageWiFiStatus();
}

// ---------------- 扫描 ----------------

void WiFiModePage::enterScanState(bool rescan) {
    state = WSTATE_SCAN;
    applyVisibility();
    scanMenu->clear();
    scanMenu->addMenuItem("扫描中...");
    scanItems.clear();
    scanHandled = false;
    scanStartMs = millis();
    if (rescan || wifiManager.isScanComplete()) {
        wifiManager.startScan();
        if (!rescan) {
            // 首次进入且上次结果仍有效可直接用, 这里统一重扫保证新鲜
        }
    } else {
        // 已有扫描在进行, 等update消费
    }
    updateDisplay();
}

void WiFiModePage::refreshScanList() {
    wifiManager.getScanResultsCopy(scanItems);
    scanMenu->clear();
    if (scanItems.empty()) {
        scanMenu->addMenuItem("未找到网络");
    } else {
        for (size_t i = 0; i < scanItems.size(); i++) {
            String name = scanItems[i].ssid;
            if (name.length() == 0) name = "(隐藏网络)";
            String item = truncateFor128(name, 12) + " " + String(scanItems[i].rssi) + "dBm";
            if (scanItems[i].encryptionType == WIFI_AUTH_OPEN) item += "(开)";
            scanMenu->addMenuItem(item);
        }
    }
    scanMenu->addMenuItem("重扫");
    scanMenu->menuSel = 0;
    scanMenu->menuStart = 0;
    scanMenu->menuPos = 0;
}

void WiFiModePage::pickScanItem(int menuIndex) {
    int n = (int)scanItems.size();
    // 菜单结构: [0..n-1]=网络, [n]=重新扫描(空列表时[0]=未找到,[1]=重扫)
    int rescanIndex = (int)scanMenu->itemCount() - 1;
    if (menuIndex == rescanIndex) {
        scanMenu->clear();
        scanMenu->addMenuItem("扫描中...");
        scanItems.clear();
        scanHandled = false;
        scanStartMs = millis();
        wifiManager.startScan();
        return;
    }
    if (menuIndex < 0 || menuIndex >= n) return;
    String ssid = scanItems[menuIndex].ssid;
    if (ssid.length() == 0) return;
    String prefill = (ssid == wifiSSID) ? wifiPassword : "";
    enterPasswordInput(ssid, prefill, true);
}

// ---------------- 密码输入 ----------------

void WiFiModePage::enterPasswordInput(const String& ssid, const String& prefill, bool fromScan) {
    pendingSsid = ssid;
    pwdFromScan = fromScan;
    String t = "密码:" + ssid;
    if (t.length() > 20) t = t.substring(0, 20);
    pwdInput->setTitle(t);
    pwdInput->setText(prefill);
    state = WSTATE_INPUT_PWD;
    applyVisibility();
}

void WiFiModePage::confirmPassword() {
    String pwd = pwdInput->getText();
    pwd.trim();
    // 空密码允许(开放网络), 直接连接
    String ssid = pendingSsid.length() > 0 ? pendingSsid : wifiSSID;
    if (ssid.length() == 0) {
        state = WSTATE_VIEW;
        applyVisibility();
        updateDisplay();
        return;
    }
    startConnect(ssid, pwd);
    pendingSsid = "";
    state = WSTATE_VIEW;
    selectedItem = 0;
    applyVisibility();
    updateDisplay();
}

// ---------------- 按键 ----------------

void WiFiModePage::onButtonBack(void* context) {
    (void)context;
    if (state == WSTATE_VIEW) {
        navBar->showLeftBlink(1, 80, 80, [this]() {
            saveSettings();
            updateHomePageWiFiStatus();
            uiEngine.navigateBack();
        });
    } else if (state == WSTATE_SCAN) {
        scanMenu->getNavBar()->showLeftBlink(1, 80, 80, [this]() {
            state = WSTATE_VIEW;
            // 回显期间Web可能已改配置, 同步一次
            SystemConfig c = systemSetting.getConfig();
            wifiEnabled = c.WifiEnabled;
            wifiSSID = c.WifiName;
            wifiPassword = c.WifiPassword;
            applyVisibility();
            updateDisplay();
        });
    } else if (state == WSTATE_INPUT_PWD) {
        inputNavBar->showLeftBlink(1, 80, 80, [this]() {
            if (pwdFromScan) {
                state = WSTATE_SCAN;
            } else {
                state = WSTATE_VIEW;
            }
            pendingSsid = "";
            applyVisibility();
            updateDisplay();
        });
    }
}

void WiFiModePage::onButtonEnter(void* context) {
    (void)context;
    if (state == WSTATE_VIEW) {
        int sel = selectedItem;
        navBar->showRightBlink(1, 80, 80, [this, sel]() {
            if (sel == 0) {
                enterScanState(true);
            } else if (sel == 1) {
                if (wifiSSID.length() == 0) enterScanState(true);
                else enterPasswordInput(wifiSSID, wifiPassword, false);
            } else {
                toggleWiFi();
            }
        });
    } else if (state == WSTATE_SCAN) {
        int sel = scanMenu->menuSel;
        scanMenu->getNavBar()->showRightBlink(1, 80, 80, [this, sel]() {
            pickScanItem(sel);
        });
    } else if (state == WSTATE_INPUT_PWD) {
        inputNavBar->showRightBlink(1, 80, 80, [this]() {
            confirmPassword();
        });
    }
}

void WiFiModePage::onButtonMenu(void* context) {
    if (state == WSTATE_SCAN) {
        scanMenu->moveDown();
    } else if (state == WSTATE_INPUT_PWD) {
        pwdInput->deleteChar();
    } else {
        // VIEW态MENU同样下移, 方便单手操作
        selectedItem++;
        if (selectedItem >= WIFI_VIEW_ROWS) selectedItem = 0;
        updateDisplay();
    }
    (void)context;
}

void WiFiModePage::onButton1(void* context) {
    (void)context;
    if (state == WSTATE_INPUT_PWD) pwdInput->onButtonPress(1);
}

void WiFiModePage::onButton2(void* context) {
    (void)context;
    if (state == WSTATE_VIEW) {
        selectedItem--;
        if (selectedItem < 0) selectedItem = WIFI_VIEW_ROWS - 1;
        updateDisplay();
    } else if (state == WSTATE_SCAN) {
        scanMenu->moveUp();
    } else if (state == WSTATE_INPUT_PWD) {
        pwdInput->onButtonPress(2);
    }
}

void WiFiModePage::onButton3(void* context) {
    (void)context;
    if (state == WSTATE_INPUT_PWD) pwdInput->onButtonPress(3);
}

void WiFiModePage::onButton4(void* context) {
    (void)context;
    if (state == WSTATE_VIEW) {
        if (selectedItem == 2 && wifiEnabled) toggleWiFi();
    } else if (state == WSTATE_INPUT_PWD) {
        pwdInput->onButtonPress(4);
    }
}

void WiFiModePage::onButton5(void* context) {
    if (state == WSTATE_INPUT_PWD) {
        pwdInput->onButtonPress(5);
    } else if (state == WSTATE_VIEW) {
        onButtonEnter(context);
    }
    (void)context;
}

void WiFiModePage::onButton6(void* context) {
    (void)context;
    if (state == WSTATE_VIEW) {
        if (selectedItem == 2 && !wifiEnabled) toggleWiFi();
    } else if (state == WSTATE_INPUT_PWD) {
        pwdInput->onButtonPress(6);
    }
}

void WiFiModePage::onButton7(void* context) {
    (void)context;
    if (state == WSTATE_INPUT_PWD) pwdInput->onButtonPress(7);
}

void WiFiModePage::onButton8(void* context) {
    (void)context;
    if (state == WSTATE_VIEW) {
        selectedItem++;
        if (selectedItem >= WIFI_VIEW_ROWS) selectedItem = 0;
        updateDisplay();
    } else if (state == WSTATE_SCAN) {
        scanMenu->moveDown();
    } else if (state == WSTATE_INPUT_PWD) {
        pwdInput->onButtonPress(8);
    }
}

void WiFiModePage::onButton9(void* context) {
    (void)context;
    if (state == WSTATE_INPUT_PWD) pwdInput->onButtonPress(9);
}

void WiFiModePage::update() {
    if (pwdInput) pwdInput->update();
    unsigned long now = millis();

    // 扫描结果消费(含超时保护)
    if (state == WSTATE_SCAN && !scanHandled) {
        if (wifiManager.isScanComplete()) {
            scanHandled = true;
            refreshScanList();
        } else if (now - scanStartMs > WIFI_SCAN_TIMEOUT_MS) {
            scanHandled = true;
            scanMenu->clear();
            scanMenu->addMenuItem("扫描超时");
            scanMenu->addMenuItem("重扫");
        }
    }

    // 每500ms轮询连接状态
    if (now - lastUpdateTime >= 500) {
        lastUpdateTime = now;
        wl_status_t st = WiFi.status();
        bool connecting = (wifiEnabled && st != WL_CONNECTED);
        if (st != lastWiFiStatus || connecting != lastConnecting) {
            lastWiFiStatus = st;
            lastConnecting = connecting;
            if (state == WSTATE_VIEW) updateDisplay();
            else refreshWiFiInfo();
        } else if (state == WSTATE_VIEW) {
            // 连接中文案(连接中.../超时)需要定时推进
            if (connectStartMs > 0 || connectTimedOut) updateDisplay();
        }

        // VIEW态下同步Web端改动(每轮都比对, 开销极小)
        if (state == WSTATE_VIEW) {
            SystemConfig c = systemSetting.getConfig();
            if (c.WifiName != wifiSSID || c.WifiPassword != wifiPassword || c.WifiEnabled != wifiEnabled) {
                // 本页刚发起的连接已实时写入config, 此处只会捕获Web/其他页面改动
                wifiSSID = c.WifiName;
                wifiPassword = c.WifiPassword;
                wifiEnabled = c.WifiEnabled;
                if (!wifiEnabled) { connectStartMs = 0; connectTimedOut = false; }
                updateDisplay();
            }
        }
    }
}
