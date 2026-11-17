/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

// SettingPage.cpp
#include "SettingPage.h"
#include "../../GUI/UIEngine.h"
#include "VersionPage.h"
#include "BrightnessPage.h"
#include "SoundPage.h"
#include "PowerSavePage.h"
#include "../Rf/RepeatTransmitPage.h"
#include "../Rf/TxPowerPage.h"
#include "../Rf/RxSensitivityPage.h"
#include "APModePage.h"
#include "WiFiModePage.h"
#include "../Data/FactoryResetPage.h"
#include "OTAPage.h"
#include "../Data/BackupPage.h"
#include "../Home/HomeManagerPage.h"
#include "BootAnimPage.h"

extern UIEngine uiEngine;

// 设置项定义
const char* SettingPage::settingItem[] = {"1.主页管理","2.开机动画","3.屏幕亮度","4.声音提示","5.节能设置","6.重复发送","7.发射功率","8.接收灵敏度","9.AP模式","10.WIFI模式","11.备份管理","12.恢复出厂","13.系统版本","14.固件更新"};
const int SettingPage::settingItemCount = 14;

SettingPage::SettingPage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT),
settingMenu(new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5)) {
    initLayout();
}


void SettingPage::initLayout() {
    // 初始化设置菜单
    settingMenu->bShowBtnTips = true;
    settingMenu->getNavBar()->setLeftButtonText("返回");
    settingMenu->getNavBar()->setRightButtonText("确定");
    settingMenu->bShowBorder = false;
    settingMenu->bShowTitle = false;

    for(int i = 0; i < settingItemCount; i++) {
        settingMenu->addMenuItem(settingItem[i]);
    }

    addWidget(settingMenu);
}

void SettingPage::onButtonBack(void* context) {
    // 显示左键闪烁动画，动画完成后执行跳转
    settingMenu->getNavBar()->showLeftBlink(1, 80, 80, [this]() {
        uiEngine.navigateBack();
    });
}

void SettingPage::onButtonEnter(void* context) {
    int selectedItem = settingMenu->menuSel;
    // 显示右键闪烁动画，动画完成后处理设置
    settingMenu->getNavBar()->showRightBlink(1, 80, 80, [this, selectedItem]() {
        switch (selectedItem) {
            case 0:
                uiEngine.navigateTo(new HomeManagerPage());
                break;
            case 1:
                uiEngine.navigateTo(new BootAnimPage());
                break;
            case 2:
                uiEngine.navigateTo(new BrightnessPage());
                break;
            case 3:
                uiEngine.navigateTo(new SoundPage());
                break;
            case 4:
                uiEngine.navigateTo(new PowerSavePage());
                break;
            case 5:
                uiEngine.navigateTo(new RepeatTransmitPage());
                break;
            case 6:
                uiEngine.navigateTo(new TxPowerPage());
                break;
            case 7:
                uiEngine.navigateTo(new RxSensitivityPage());
                break;
            case 8:
                uiEngine.navigateTo(new APModePage());
                break;
            case 9:
                uiEngine.navigateTo(new WiFiModePage());
                break;
            case 10:
                uiEngine.navigateTo(new BackupPage());
                break;
            case 11:
                uiEngine.navigateTo(new FactoryResetPage());
                break;
            case 12:
                uiEngine.navigateTo(new VersionPage());
                break;
            case 13:
                uiEngine.navigateTo(new OTAPage());
                break;
            default:
                break;
        }
    });
}

void SettingPage::onButton5(void* context) {
    onButtonEnter(context);
}

void SettingPage::onButton2(void* context) {
    settingMenu->moveUp();
}

void SettingPage::onButton8(void* context) {
    settingMenu->moveDown();
}
