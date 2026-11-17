/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

// MenuPage.cpp
#include "MenuPage.h"
#include "../../GUI/UIEngine.h"
#include "HomePage.h"
#include "../Settings/SettingPage.h"
#include "../Rf/ReceivePage.h"
#include "../Rf/FrequencySelectPage.h"
#include "../Rf/SendDataPage.h"
#include "../Data/ManageDataMenuPage.h"
#include "../Ir/IRMenuPage.h"
#include "../Audio/MusicBrowserPage.h"
#include "../Games/GameListPage.h"
#include "../Audio/RecordSourcePage.h"
#include "../Talk/TalkMenuPage.h"
#include "HomeSwitchPage.h"
#include "../Rf/RFMenuPage.h"
#include "../../GUI/Animation/MessageBoxAnimation.h"
#include "../../Services/WiFiManager.h"
#include "../../Services/SystemSetting.h"
#include "../../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "MENU"

extern UIEngine uiEngine;
extern WiFiManager wifiManager;
extern SystemSetting systemSetting;
extern HomePage uiPageHome;

// 菜单项定义（2.射频遥控为二级菜单，与红外逻辑一致；管理数据为三级分类入口）
const char* menuItem[] = {"1.主页切换","2.射频遥控","3.红外遥控","4.管理数据","5.音乐播放","6.声音录制","7.无线对讲","8.系统设置","9.休闲游戏"};
const int menuItemCount = sizeof(menuItem) / sizeof(menuItem[0]);

MenuPage::MenuPage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT),
menu(nullptr),
radioConfirmBox(nullptr) {
    initLayout();
}

void MenuPage::initLayout() {
    menu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    menu->bShowBtnTips = true;
    menu->bShowBorder = false;
    menu->bShowTitle = false;
    menu->getNavBar()->setLeftButtonText("返回");
    menu->getNavBar()->setRightButtonText("选择");
    for(int i = 0; i < menuItemCount; i++) {
        menu->addMenuItem(menuItem[i]);
    }
    addWidget(menu);

    // 录音/播放前的射频干扰确认框（WIFI/AP/BT 任一活跃时弹出）
    // 直接用 UIMessageBox 自带底部双按钮（忽略/关闭），不再外挂 UINavBar，避免底部按钮条与框体重叠
    // 无标题+2行消息+按钮行共约52px，112x56 居中刚好容下
    radioConfirmBox = new UIMessageBox(-1, -1, 112, 56);
    radioConfirmBox->setDefaultPosition(SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    radioConfirmBox->bShowTitle = false;
    radioConfirmBox->setMessage("WIFI/AP已开启\n有杂音，关闭吗？");
    radioConfirmBox->setButtons("忽略", "关闭");
    radioConfirmBox->bVisible = false;
    addWidget(radioConfirmBox);

    currentState = STATE_BROWSE;
    pendingIndex = -1;
}

void MenuPage::showPage() {
    UIPage::showPage();
    currentState = STATE_BROWSE;
    pendingIndex = -1;
    if (menu) menu->bVisible = true;
    if (radioConfirmBox) { radioConfirmBox->hide(false); radioConfirmBox->bVisible = false; }
}

void MenuPage::update() {
    if (radioConfirmBox) radioConfirmBox->update();
}

void MenuPage::render(U8G2* u8g2) {
    UIPage::render(u8g2);
    update();
}

void MenuPage::navigateToTarget(int index) {
    switch (index) {
        case 0:
            uiEngine.navigateTo(new HomeSwitchPage());
            break;
        case 1:
            uiEngine.navigateTo(new RFMenuPage());
            break;
        case 2:
            uiEngine.navigateTo(new IRMenuPage());
            break;
        case 3:
            uiEngine.navigateTo(new ManageDataMenuPage());
            break;
        case 4:
            uiEngine.navigateTo(new MusicBrowserPage());
            break;
        case 5:
            uiEngine.navigateTo(new RecordSourcePage());
            break;
        case 6:
            uiEngine.navigateTo(new TalkMenuPage());
            break;
        case 7:
            uiEngine.navigateTo(new SettingPage());
            break;
        case 8:
            uiEngine.navigateTo(new GameListPage());
            break;
        default:
            break;
    }
}

bool MenuPage::checkRadioAndPrompt(int index) {
    // 仅音乐播放(4)与声音录制(5)需要检查(射频开着会有杂音);
    // 无线对讲(6)自己管理射频(进页开STA/离页关), 不弹窗直接进。
    if (index != 4 && index != 5) return false;
    if (!wifiManager.isAnyRadioActive()) return false;

    pendingIndex = index;
    if (menu) menu->bVisible = false;
    radioConfirmBox->show(MSGBOX_ANIME_ZOOM_CENTER, 300);
    currentState = STATE_RADIO_CONFIRM;
    LOG_W("MenuPage: WIFI/AP/BT已开启，弹出杂音干扰确认框");
    return true;
}

void MenuPage::dismissRadioConfirm(bool closeRadio) {
    int target = pendingIndex;
    pendingIndex = -1;
    radioConfirmBox->hide(false);
    radioConfirmBox->bVisible = false;
    if (menu) menu->bVisible = true;
    currentState = STATE_BROWSE;

    if (closeRadio) {
        LOG_I("MenuPage: 用户选择关闭射频");
        // 走SystemSetting关闭: 同步内存+NVS配置(设置页/Web端/开机自连全部
        // 一致显示"关闭"), 内部触发disconnect/stopAP实际断开
        systemSetting.setWifiEnabled(false, true);
        systemSetting.setAPEnabled(false, true);
        // 兜底: 覆盖"配置开着但未连上/正在连接"的射频活跃态, 彻底关闭
        // WiFi+BT并清空worker在途命令(防命令消费时把射频重新拉起)
        wifiManager.disableAllRadio();
        // 主页状态图标同步(WiFi/AP已关)
        uiPageHome.updateStatus();
    } else {
        LOG_I("MenuPage: 用户选择忽略，保留射频");
    }
    navigateToTarget(target);
}

void MenuPage::onButtonBack(void* context) {
    if (currentState == STATE_RADIO_CONFIRM) {
        radioConfirmBox->showLeftButtonHighLight([this]() {
            dismissRadioConfirm(false);
        });
        return;
    }
    menu->getNavBar()->showLeftBlink(1, 80, 80, [this]() {
        uiEngine.navigateBack();
    });
}

void MenuPage::onButtonMenu(void* context) {
    if (currentState == STATE_RADIO_CONFIRM) return;
    menu->moveDown();
}

void MenuPage::onButtonEnter(void* context) {
    if (currentState == STATE_RADIO_CONFIRM) {
        radioConfirmBox->showRightButtonHighLight([this]() {
            dismissRadioConfirm(true);
        });
        return;
    }
    int selectedItem = menu->menuSel;
    menu->getNavBar()->showRightBlink(1, 80, 80, [this, selectedItem]() {
        if (checkRadioAndPrompt(selectedItem)) return;
        navigateToTarget(selectedItem);
    });
}

void MenuPage::onButton2(void* context) {
    if (currentState == STATE_RADIO_CONFIRM) return;
    menu->moveUp();
}

void MenuPage::onButton5(void* context) {
    onButtonEnter(context);
}

void MenuPage::onButton8(void* context) {
    if (currentState == STATE_RADIO_CONFIRM) return;
    menu->moveDown();
}
