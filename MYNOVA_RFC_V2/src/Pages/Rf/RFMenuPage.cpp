/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#include "RFMenuPage.h"
#include "../../GUI/UIEngine.h"
#include "../../GUI/GUIRender.h"
#include "FrequencySelectPage.h"
#include "SendDataPage.h"
#include "RawConfigPage.h"

extern UIEngine uiEngine;

RFMenuPage::RFMenuPage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT),
    rfMenu(nullptr) {
    initLayout();
}

RFMenuPage::~RFMenuPage() {
}

void RFMenuPage::initLayout() {
    rfMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    rfMenu->titleText = "射频遥控";
    rfMenu->bShowBtnTips = true;
    rfMenu->bShowBorder = false;
    rfMenu->bShowTitle = false;
    rfMenu->bVisible = true;

    rfMenu->getNavBar()->setLeftButtonText("返回");
    rfMenu->getNavBar()->setRightButtonText("选择");

    rfMenu->addMenuItem("1. RAW录制");
    rfMenu->addMenuItem("2. 射频接收");
    rfMenu->addMenuItem("3. 射频发送");
    addWidget(rfMenu);
}

void RFMenuPage::onButtonBack(void* context) {
    rfMenu->getNavBar()->showLeftBlink(1, 80, 80, [this]() {
        uiEngine.navigateBack();
    });
}

void RFMenuPage::onButtonMenu(void* context) {
    rfMenu->moveDown();
}

void RFMenuPage::onButtonEnter(void* context) {
    int selectedItem = rfMenu->menuSel;
    rfMenu->getNavBar()->showRightBlink(1, 80, 80, [this, selectedItem]() {
        switch (selectedItem) {
            case 0:
                uiEngine.navigateTo(new RawConfigPage());
                break;
            case 1:
                uiEngine.navigateTo(new FrequencySelectPage());
                break;
            case 2:
                uiEngine.navigateTo(new SendDataPage(FILTER_RF));
                break;
            default:
                break;
        }
    });
}

void RFMenuPage::onButton2(void* context) {
    rfMenu->moveUp();
}

void RFMenuPage::onButton5(void* context) {
    onButtonEnter(context);
}

void RFMenuPage::onButton8(void* context) {
    rfMenu->moveDown();
}
