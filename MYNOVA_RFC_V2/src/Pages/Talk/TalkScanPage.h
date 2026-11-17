/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// TalkScanPage.h - 设备搜索/配对页
// 进页自动startScan并广播BEACON; 列表显示 设备名+忙闲(+MAC尾缀), 2/8上下选,
// 5/ENTER发起连接(等对端确认), 来电时本页弹窗 ENTER接受/BACK拒绝。
#ifndef TalkScanPage_h
#define TalkScanPage_h

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIMenu.h"
#include "../../GUI/Widget/UIMessageBox.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../GUI/GUIRender.h"
#include "../../Services/TalkManager.h"

class TalkScanPage : public UIPage {
public:
    TalkScanPage();
    ~TalkScanPage() override;

    void showPage() override;
    void update() override;
    void render(U8G2* u8g2) override;

    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;

private:
    enum ScanState { SC_SCAN, SC_WAIT_ACCEPT, SC_INCOMING, SC_ALERT };
    void refreshList();            // 从TalkManager同步列表到UIMenu
    void showIncoming();           // 来电弹窗
    void hideIncoming();
    void showDropAlert();          // 对端掉线/离开一次性提示(2.5秒自动关闭)

    UIMenu* listMenu = nullptr;
    UIMessageBox* incomingBox = nullptr;
    UINavBar* incomingNav = nullptr;
    ScanState state = SC_SCAN;
    unsigned long lastRefreshMs = 0;
    int lastCount = -1;
    int connRows = 0;              // 首行已连接断开项行数(0/1, 选择索引需跳过)
    TalkLinkState lastLink = TALK_LINK_IDLE;
    // 发起配对的目标MAC(等ACK期间显示"等待确认")
    uint8_t waitMac[6];
    bool hasWaitMac = false;
    unsigned long seenDropMs = 0;  // 已提示过的掉线事件时间戳(每页只弹一次)
};

#endif
