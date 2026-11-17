/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

// RxSensitivityPage.cpp
#include "RxSensitivityPage.h"
#include "../../GUI/UIEngine.h"
#include "../../Services/SystemSetting.h"
#include "../../Drivers/RadioHelper.h"

extern UIEngine uiEngine;
extern SystemSetting systemSetting;
extern RadioHelper radioHelper;

static const char* RX_DESC[3] = {"标准(抗干扰)", "高(默认)", "最高(弱信号)"};

RxSensitivityPage::RxSensitivityPage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT) {
    currentLevel = systemSetting.getRxSensitivity();
    if (currentLevel < 0 || currentLevel > 2) currentLevel = 1;
    originalLevel = currentLevel;

    initLayout();
}

void RxSensitivityPage::initLayout() {
    prefixLabel = new UILabel();
    prefixLabel->label = "接收灵敏度";
    prefixLabel->textFont = u8g2_font_wqy12_t_gb2312;
    prefixLabel->textAlign = RIGHT;
    prefixLabel->verticalAlign = MIDDLE;
    prefixLabel->height = 12;
    prefixLabel->y = 16;
    addWidget(prefixLabel);

    numberEdit = new UIEditableNumber();
    numberEdit->setMode(EDITABLE_DECIMAL);
    numberEdit->setMaxDigits(1);
    numberEdit->setRange(0, 2);
    numberEdit->setValue(currentLevel);
    numberEdit->setSelected(true);
    numberEdit->textFont = u8g2_font_wqy12_t_gb2312;
    numberEdit->width = 15;
    numberEdit->height = 12;
    numberEdit->y = 16;
    numberEdit->bVisible = true;
    addWidget(numberEdit);

    suffixLabel = new UILabel();
    suffixLabel->label = "档";
    suffixLabel->textFont = u8g2_font_wqy12_t_gb2312;
    suffixLabel->textAlign = LEFT;
    suffixLabel->verticalAlign = MIDDLE;
    suffixLabel->height = 12;
    suffixLabel->y = 16;
    addWidget(suffixLabel);

    descLabel = new UILabel();
    descLabel->textFont = u8g2_font_wqy12_t_gb2312;
    descLabel->textAlign = CENTER;
    descLabel->verticalAlign = MIDDLE;
    descLabel->width = SCREEN_WIDTH;
    descLabel->x = 0;
    descLabel->height = 12;
    descLabel->y = 34;
    addWidget(descLabel);

    int prefixWidth = 60;
    int numberWidth = 15;
    int suffixWidth = 12;
    int totalWidth = prefixWidth + numberWidth + suffixWidth;
    int startX = (SCREEN_WIDTH - totalWidth) / 2;

    prefixLabel->x = startX;
    prefixLabel->width = prefixWidth;
    numberEdit->x = startX + prefixWidth + 5;
    suffixLabel->x = startX + prefixWidth + numberWidth + 10;
    suffixLabel->width = suffixWidth;

    navBar = new UINavBar(0, SCREEN_HEIGHT - 12, SCREEN_WIDTH, 12);
    navBar->setLeftButtonText("取消");
    navBar->setRightButtonText("确定");
    addWidget(navBar);

    updateLevel();
}

void RxSensitivityPage::updateLevel() {
    currentLevel = numberEdit->getValue();
    if (currentLevel < 0) currentLevel = 0;
    if (currentLevel > 2) currentLevel = 2;
    descLabel->label = RX_DESC[currentLevel];
    radioHelper.SetRxSensitivity(currentLevel);
}

void RxSensitivityPage::restoreLevel() {
    currentLevel = originalLevel;
    numberEdit->setValue(currentLevel);
    radioHelper.SetRxSensitivity(currentLevel);
}

void RxSensitivityPage::saveLevel() {
    currentLevel = numberEdit->getValue();
    systemSetting.setRxSensitivity(currentLevel, true);
    radioHelper.SetRxSensitivity(currentLevel);
}

void RxSensitivityPage::onButtonBack(void* context) {
    navBar->showLeftBlink(1, 80, 80, [this]() {
        restoreLevel();
        uiEngine.navigateBack();
    });
}

void RxSensitivityPage::onButtonEnter(void* context) {
    navBar->showRightBlink(1, 80, 80, [this]() {
        saveLevel();
        uiEngine.navigateBack();
    });
}

void RxSensitivityPage::onButton5(void* context) {
    onButtonEnter(context);
}

void RxSensitivityPage::onButton2(void* context) {
    numberEdit->incrementDigit();
    updateLevel();
}

void RxSensitivityPage::onButton4(void* context) {
    numberEdit->decrementDigit();
    updateLevel();
}

void RxSensitivityPage::onButton6(void* context) {
    numberEdit->incrementDigit();
    updateLevel();
}

void RxSensitivityPage::onButton8(void* context) {
    numberEdit->decrementDigit();
    updateLevel();
}
