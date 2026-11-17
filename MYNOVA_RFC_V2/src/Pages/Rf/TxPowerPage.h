/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

// TxPowerPage.h — 发射功率档（0-7，7=最大+10dBm）
#ifndef TxPowerPage_h
#define TxPowerPage_h

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UILabel.h"
#include "../../GUI/Widget/UIEditableNumber.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../GUI/GUIRender.h"

class TxPowerPage : public UIPage {
public:
    TxPowerPage();

    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton4(void* context = nullptr) override;
    void onButton6(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;

private:
    void initLayout();
    void updatePower();
    void restorePower();
    void savePower();

    UILabel* prefixLabel;
    UIEditableNumber* numberEdit;
    UILabel* suffixLabel;
    UILabel* dbmLabel;
    UINavBar* navBar;

    int currentPower;
    int originalPower;
};

#endif
