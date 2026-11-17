/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

// RxSensitivityPage.h — 接收灵敏度（0=标准 1=高 2=最高）
#ifndef RxSensitivityPage_h
#define RxSensitivityPage_h

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UILabel.h"
#include "../../GUI/Widget/UIEditableNumber.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../GUI/GUIRender.h"

class RxSensitivityPage : public UIPage {
public:
    RxSensitivityPage();

    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton4(void* context = nullptr) override;
    void onButton6(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;

private:
    void initLayout();
    void updateLevel();
    void restoreLevel();
    void saveLevel();

    UILabel* prefixLabel;
    UIEditableNumber* numberEdit;
    UILabel* suffixLabel;
    UILabel* descLabel;
    UINavBar* navBar;

    int currentLevel;
    int originalLevel;
};

#endif
