/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

// TxPowerPage.cpp
#include "TxPowerPage.h"
#include "../../GUI/UIEngine.h"
#include "../../Services/SystemSetting.h"
#include "../../Drivers/RadioHelper.h"
#include "../../Lib/CC1101Driver.h"

extern UIEngine uiEngine;
extern SystemSetting systemSetting;
extern RadioHelper radioHelper;

TxPowerPage::TxPowerPage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT) {
    currentPower = systemSetting.getTxPowerLevel();
    if (currentPower < 0 || currentPower > 7) currentPower = 7;
    originalPower = currentPower;

    initLayout();
}

void TxPowerPage::initLayout() {
    prefixLabel = new UILabel();
    prefixLabel->label = "发射功率";
    prefixLabel->textFont = u8g2_font_wqy12_t_gb2312;
    prefixLabel->textAlign = RIGHT;
    prefixLabel->verticalAlign = MIDDLE;
    prefixLabel->height = 12;
    prefixLabel->y = 16;
    addWidget(prefixLabel);

    numberEdit = new UIEditableNumber();
    numberEdit->setMode(EDITABLE_DECIMAL);
    numberEdit->setMaxDigits(1);
    numberEdit->setRange(0, 7);
    numberEdit->setValue(currentPower);
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

    dbmLabel = new UILabel();
    dbmLabel->textFont = u8g2_font_wqy12_t_gb2312;
    dbmLabel->textAlign = CENTER;
    dbmLabel->verticalAlign = MIDDLE;
    dbmLabel->width = SCREEN_WIDTH;
    dbmLabel->x = 0;
    dbmLabel->height = 12;
    dbmLabel->y = 34;
    addWidget(dbmLabel);

    int prefixWidth = 48;
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

    updatePower();
}

void TxPowerPage::updatePower() {
    currentPower = numberEdit->getValue();
    if (currentPower < 0) currentPower = 0;
    if (currentPower > 7) currentPower = 7;
    char buf[32];
    snprintf(buf, sizeof(buf), "约 %d dBm (7最大)",
             CC1101Driver::txPowerToDbm(currentPower));
    dbmLabel->label = buf;
    radioHelper.SetTxPowerLevel(currentPower);
}

void TxPowerPage::restorePower() {
    currentPower = originalPower;
    numberEdit->setValue(currentPower);
    radioHelper.SetTxPowerLevel(currentPower);
}

void TxPowerPage::savePower() {
    currentPower = numberEdit->getValue();
    systemSetting.setTxPowerLevel(currentPower, true);
    radioHelper.SetTxPowerLevel(currentPower);
}

void TxPowerPage::onButtonBack(void* context) {
    navBar->showLeftBlink(1, 80, 80, [this]() {
        restorePower();
        uiEngine.navigateBack();
    });
}

void TxPowerPage::onButtonEnter(void* context) {
    navBar->showRightBlink(1, 80, 80, [this]() {
        savePower();
        uiEngine.navigateBack();
    });
}

void TxPowerPage::onButton5(void* context) {
    onButtonEnter(context);
}

void TxPowerPage::onButton2(void* context) {
    numberEdit->incrementDigit();
    updatePower();
}

void TxPowerPage::onButton4(void* context) {
    numberEdit->decrementDigit();
    updatePower();
}

void TxPowerPage::onButton6(void* context) {
    numberEdit->incrementDigit();
    updatePower();
}

void TxPowerPage::onButton8(void* context) {
    numberEdit->decrementDigit();
    updatePower();
}
