/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef __SAVEDATAPAGE_H__
#define __SAVEDATAPAGE_H__

#include <Arduino.h>
#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIMenu.h"
#include "../../GUI/Widget/UIMessageBox.h"
#include "../../GUI/Widget/UIInput.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../GUI/GUIRender.h"
#include "../../Services/DataStore.h"
#include "../../Proto/SignalData.h"

enum SaveDataPageState {
    STATE_SELECT,      // 选择位置状态
    STATE_MESSASGE,    // 确认覆盖状态
    STATE_INPUTNAME,   // 输入名字状态
    STATE_SAVE_FAILED, STATE_SAVE_SUCCESS // 保存成功状态
};

/**
 * SaveDataPage - 信号保存页面（射频/红外统一）
 * 流程：选择存储位置(1-100) -> 覆盖确认 -> 输入名称 -> 保存
 * 射频/红外信号共用同一套槽位与保存流程
 */
class SaveDataPage : public UIPage {
public:
    SaveDataPage(const SignalDraft& draft, int suggestedSlot = -1, int backSteps = 2);
    ~SaveDataPage();
    void setSuggestedSlot(int slot);
    
    void initLayout();
    void render(U8G2* u8g2) override;
    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButton1(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton3(void* context = nullptr) override;
    void onButton4(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton6(void* context = nullptr) override;
    void onButton7(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;
    void onButton9(void* context = nullptr) override;

private:
    UIMenu* dataListMenu;
    UIMessageBox* messageBox;
    UIMessageBox* successMessageBox;  // 保存成功提示框
    UIInput* nameInput;
    UINavBar* inputNavBar;
    SignalDraft draft;                // 待保存的信号数据
    int suggestedSlot = -1;
    int savedBackSteps = 2;           // 保存成功后回退的页面层数（入口深度不同）
    SaveDataPageState currentState;   // 当前页面状态
    String generateDefaultName(int index);
    void saveDataAndReturn();
};

#endif
