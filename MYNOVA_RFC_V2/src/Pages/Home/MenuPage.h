/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

// MenuPage.h
#ifndef MenuPage_h
#define MenuPage_h

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIMenu.h"
#include "../../GUI/Widget/UIMessageBox.h"
#include "../../GUI/GUIRender.h"
#include "../Rf/SendDataPage.h"

class MenuPage : public UIPage {
public:
    MenuPage();

    void showPage() override;
    void update() override;
    void render(U8G2* u8g2) override;

    // 重写按钮事件处理
    void onButtonBack(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;

private:
    void initLayout(); // 初始化页面布局
    void navigateToTarget(int index);           // 实际跳转
    bool checkRadioAndPrompt(int index);        // 需要弹窗则返回true
    void dismissRadioConfirm(bool closeRadio);  // 忽略/关闭 后的收尾

    enum MenuState { STATE_BROWSE, STATE_RADIO_CONFIRM };
    MenuState currentState = STATE_BROWSE;
    int pendingIndex = -1; // 等待确认后跳转的目标index

    UIMenu *menu = nullptr;
    UIMessageBox* radioConfirmBox = nullptr;
};

#endif