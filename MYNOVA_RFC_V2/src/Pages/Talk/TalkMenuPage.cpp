/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// TalkMenuPage.cpp - 无线对讲二级菜单实现
//   停留本页时也监听来电(对端直连请求): 弹窗确认, 5/ENTER接受进通话页, BACK拒绝。
//   (此前仅扫描页能收来电: A在菜单页停留时B发起, A侧超时无感 → 补上此缺口)
#include "TalkMenuPage.h"
#include "TalkScanPage.h"
#include "TalkVoicePage.h"
#include "TalkChatPage.h"
#include "TalkSettingPage.h"
#include "../../GUI/UIEngine.h"
#include "../../GUI/Animation/MessageBoxAnimation.h"
#include "../../Services/TalkManager.h"
#include "../../Drivers/AudioPlayer.h"

extern UIEngine uiEngine;

TalkMenuPage::TalkMenuPage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT) {
    initLayout();
}

TalkMenuPage::~TalkMenuPage() {
}

void TalkMenuPage::initLayout() {
    menu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    menu->titleText = "无线对讲";
    menu->bShowBtnTips = true;
    menu->bShowBorder = false;
    menu->bShowTitle = false;
    menu->bVisible = true;
    menu->getNavBar()->setLeftButtonText("返回");
    menu->getNavBar()->setRightButtonText("选择");
    menu->addMenuItem("1.连接设备");
    menu->addMenuItem("2.开始对讲");
    menu->addMenuItem("3.文字聊天");
    menu->addMenuItem("4.对讲设置");
    addWidget(menu);

    // 来电确认框(对端PAIR_REQ, 本页停留时收到)
    incomingBox = new UIMessageBox(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    incomingBox->setTitle("来电");
    incomingBox->bVisible = false;
    incomingBox->bShowButtons = false;
    addWidget(incomingBox);

    incomingNav = new UINavBar(0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, 15);
    incomingNav->setLeftButtonText("拒绝");
    incomingNav->setRightButtonText("接受");
    incomingNav->bVisible = false;
    addWidget(incomingNav);
}

void TalkMenuPage::showPage() {
    UIPage::showPage();
    currentState = STATE_BROWSE;
    incomingHandled = false;
    seenDropMs = talkManager.getLastDropMs();  // 进页前的旧掉线事件不弹窗
    hideIncoming();
    // 进对讲域: 开射频+ESP-NOW(失败由各子页提示, 菜单本身可进设置改信道)
    talkManager.begin();
}

void TalkMenuPage::showIncoming() {
    char msg[64];
    String nm = talkManager.getIncomingName();
    if (nm.length() > 24) nm = nm.substring(0, 24);
    snprintf(msg, sizeof(msg), "%s\n请求对讲\n是否接受?", nm.c_str());
    incomingBox->setMessage(msg);
    menu->bVisible = false;
    incomingBox->show(MSGBOX_ANIME_ZOOM_CENTER, 300);
    incomingNav->bVisible = true;
}

void TalkMenuPage::hideIncoming() {
    incomingBox->hide(false);
    incomingBox->bVisible = false;
    incomingNav->bVisible = false;
    menu->bVisible = true;
}

void TalkMenuPage::acceptIncoming() {
    incomingHandled = true;
    hideIncoming();
    talkManager.acceptIncoming(true);
    // 连接立即成立(acceptIncoming同步置CONNECTED), 直接进语音页开音频引擎
    uiEngine.navigateTo(new TalkVoicePage());
}

void TalkMenuPage::rejectIncoming() {
    incomingHandled = false;
    hideIncoming();
    talkManager.acceptIncoming(false);
    currentState = STATE_BROWSE;
}

void TalkMenuPage::showDropAlert() {
    // 对端掉线/离开提示: 复用来电弹窗, 无按钮2.5秒自动关闭
    currentState = STATE_ALERT;
    bool lost = talkManager.getLinkDropReason() == TALK_DROP_LOST;
    incomingBox->setTitle("提示");
    incomingBox->setMessage(lost ? "连接已断开\n对方关机或超出距离"
                                 : "连接已断开\n对端已离开");
    incomingBox->bShowButtons = false;
    incomingNav->bVisible = false;
    menu->bVisible = false;
    incomingBox->cancelAutoClose();
    incomingBox->show(MSGBOX_ANIME_ZOOM_CENTER, 200);
    incomingBox->setAutoClose(2500);
    SoundAlertBeep(200);
}

void TalkMenuPage::update() {
    if (incomingBox) incomingBox->update();
    // 掉线提示展示中: 等弹窗2.5秒自动关闭后恢复菜单, 期间冻结来电状态跳转
    if (currentState == STATE_ALERT) {
        if (!incomingBox->isVisible()) {
            currentState = STATE_BROWSE;
            menu->bVisible = true;
        }
        return;
    }
    // 来电到达(仅在浏览态弹窗; 已处理/已在通话页则忽略)
    if (currentState == STATE_BROWSE && !incomingHandled &&
        talkManager.getLinkState() == TALK_LINK_INCOMING) {
        currentState = STATE_INCOMING;
        showIncoming();
        SoundAlertBeep(150);
        return;
    }
    // 来电消失(对端取消/15秒超时) → 静默回菜单
    if (currentState == STATE_INCOMING &&
        talkManager.getLinkState() != TALK_LINK_INCOMING) {
        currentState = STATE_BROWSE;
        incomingHandled = false;
        hideIncoming();
        return;
    }
    // 新掉线事件(对端关机/超距/正常离开, 如从语音页返回菜单后): 弹一次自动关闭提示
    unsigned long dropMs = talkManager.getLastDropMs();
    if (currentState == STATE_BROWSE && dropMs != 0 && dropMs != seenDropMs) {
        seenDropMs = dropMs;
        showDropAlert();
    }
}

void TalkMenuPage::navigateToTarget(int index) {
    switch (index) {
        case 0:
            // 1.连接设备: 搜索/断开(已连接返回后保持连接)
            uiEngine.navigateTo(new TalkScanPage());
            break;
        case 1:
            // 2.开始对讲需先连接: 未连接则先进连接页(配对成功自动进语音页)
            if (talkManager.isPeerConnected()) uiEngine.navigateTo(new TalkVoicePage());
            else uiEngine.navigateTo(new TalkScanPage());
            break;
        case 2:
            uiEngine.navigateTo(new TalkChatPage());
            break;
        case 3:
            uiEngine.navigateTo(new TalkSettingPage());
            break;
        default:
            break;
    }
}

void TalkMenuPage::onButtonBack(void* context) {
    if (currentState == STATE_ALERT) return;   // 掉线提示自动关闭, 按键不响应
    if (currentState == STATE_INCOMING) {
        incomingNav->showLeftBlink(1, 80, 80, [this]() {
            rejectIncoming();
        });
        return;
    }
    menu->getNavBar()->showLeftBlink(1, 80, 80, [this]() {
        // 退出对讲域：结束ESP-NOW并恢复原来的WiFi模式。
        talkManager.end();
        uiEngine.navigateBack();
    });
}

void TalkMenuPage::onButtonMenu(void* context) {
    if (currentState != STATE_BROWSE) return;   // 来电/掉线弹窗期间冻结菜单
    menu->moveDown();
}

void TalkMenuPage::onButtonEnter(void* context) {
    if (currentState == STATE_ALERT) return;   // 掉线提示自动关闭, 按键不响应
    if (currentState == STATE_INCOMING) {
        incomingNav->showRightBlink(1, 80, 80, [this]() {
            acceptIncoming();
        });
        return;
    }
    int selectedItem = menu->menuSel;
    menu->getNavBar()->showRightBlink(1, 80, 80, [this, selectedItem]() {
        navigateToTarget(selectedItem);
    });
}

void TalkMenuPage::onButton2(void* context) {
    if (currentState != STATE_BROWSE) return;   // 来电/掉线弹窗期间冻结菜单
    menu->moveUp();
}

void TalkMenuPage::onButton5(void* context) {
    onButtonEnter(context);
}

void TalkMenuPage::onButton8(void* context) {
    if (currentState != STATE_BROWSE) return;   // 来电/掉线弹窗期间冻结菜单
    menu->moveDown();
}
