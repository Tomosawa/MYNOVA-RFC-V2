/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#include "FrequencySelectPage.h"
#include "../../Drivers/RadioHelper.h"
#include "ReceivePage.h"
#include "../../GUI/UIEngine.h"
#include "../../GUI/GUIRender.h"

extern UIEngine uiEngine;

FrequencySelectPage::FrequencySelectPage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT),
    bandMenu(nullptr) {
    initLayout();
}

FrequencySelectPage::~FrequencySelectPage() {
}

void FrequencySelectPage::initLayout() {
    // 初始化频段选择菜单
    bandMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    bandMenu->titleText = "选择接收频段";
    bandMenu->bShowBtnTips = true;
    bandMenu->bShowBorder = false;
    bandMenu->bShowTitle = false;
    bandMenu->bVisible = true;

    // 通过导航栏设置按钮文字
    bandMenu->getNavBar()->setLeftButtonText("返回");
    bandMenu->getNavBar()->setRightButtonText("选择");

    bandMenu->addMenuItem("1. 低频段 310-350");
    bandMenu->addMenuItem("2. 高频段 390-434");
    // 固定单频点守候（与 RF_FREQ_TABLE_ALL 对应，见 RadioHelper.cpp）
    bandMenu->addMenuItem("3. 频率 310MHz");
    bandMenu->addMenuItem("4. 频率 315MHz");
    bandMenu->addMenuItem("5. 频率 330MHz");
    bandMenu->addMenuItem("6. 频率 350MHz");
    bandMenu->addMenuItem("7. 频率 390MHz");
    bandMenu->addMenuItem("8. 频率 418MHz");
    bandMenu->addMenuItem("9. 频率 430.5MHz");
    bandMenu->addMenuItem("10. 频率 431.5MHz");
    bandMenu->addMenuItem("11. 频率 432.8MHz");
    bandMenu->addMenuItem("12. 频率 433.92MHz");
    addWidget(bandMenu);
}

void FrequencySelectPage::onButtonBack(void* context) {
    // 显示左键闪烁动画，动画完成后返回上一页
    bandMenu->getNavBar()->showLeftBlink(1, 80, 80, [this]() {
        uiEngine.navigateBack();
    });
}

void FrequencySelectPage::onButtonMenu(void* context) {
    bandMenu->moveDown();
}

void FrequencySelectPage::onButtonEnter(void* context) {
    // 显示右键闪烁动画，动画完成后进入接收页面：
    // 前两项为频段扫描，其余为固定单频点守候
    int selectedItem = bandMenu->menuSel;
    bandMenu->getNavBar()->showRightBlink(1, 80, 80, [this, selectedItem]() {
        if (selectedItem == 0) {
            uiEngine.navigateTo(new ReceivePage(RF_BAND_LOW));
        } else if (selectedItem == 1) {
            uiEngine.navigateTo(new ReceivePage(RF_BAND_HIGH));
        } else {
            int freqIdx = selectedItem - 2;
            if (freqIdx >= 0 && freqIdx < RF_FREQ_COUNT_ALL) {
                uiEngine.navigateTo(new ReceivePage(RF_FREQ_TABLE_ALL[freqIdx]));
            }
        }
    });
}

void FrequencySelectPage::onButton2(void* context) {
    bandMenu->moveUp();
}

void FrequencySelectPage::onButton5(void* context) {
    onButtonEnter(context);
}

void FrequencySelectPage::onButton8(void* context) {
    bandMenu->moveDown();
}
