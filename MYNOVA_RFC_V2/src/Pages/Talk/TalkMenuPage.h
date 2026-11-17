/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// TalkMenuPage.h - 无线对讲二级菜单: 开始对讲/文字聊天/搜索设备/对讲设置
// 按键: 2=上移 8/MENU=下移 5/ENTER=进入 BACK=返回
//       来电弹窗时: 5/ENTER=接受(进通话页) BACK=拒绝
#ifndef TalkMenuPage_h
#define TalkMenuPage_h

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIMenu.h"
#include "../../GUI/Widget/UIMessageBox.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../GUI/GUIRender.h"

class TalkMenuPage : public UIPage {
public:
    TalkMenuPage();
    ~TalkMenuPage() override;

    void showPage() override;
    void update() override;
    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;

private:
    void initLayout();
    void navigateToTarget(int index);
    void showIncoming();              // 来电弹窗(对端请求对讲)
    void hideIncoming();
    void acceptIncoming();            // 接受→直接进语音页
    void rejectIncoming();            // 拒绝→回菜单
    void showDropAlert();             // 对端掉线/离开一次性提示(2.5秒自动关闭)

    enum PageState { STATE_BROWSE, STATE_INCOMING, STATE_ALERT };
    PageState currentState = STATE_BROWSE;
    bool incomingHandled = false;     // 本页已处理过来电(接受后导航, 不再弹)
    unsigned long seenDropMs = 0;     // 已提示过的掉线事件时间戳(每页只弹一次)
    UIMenu* menu = nullptr;           // 输入源选择菜单
    UIMessageBox* incomingBox = nullptr;
    UINavBar* incomingNav = nullptr;
};

#endif
