/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// TalkScanPage.cpp - 设备搜索/配对页实现
#include "TalkScanPage.h"
#include "TalkVoicePage.h"
#include "../../GUI/UIEngine.h"
#include "../../GUI/Animation/MessageBoxAnimation.h"
#include "../../Drivers/AudioPlayer.h"

extern UIEngine uiEngine;

TalkScanPage::TalkScanPage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT) {
    listMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    listMenu->bShowBtnTips = true;
    listMenu->bShowBorder = false;
    listMenu->bShowTitle = false;
    listMenu->getNavBar()->setLeftButtonText("返回");
    listMenu->getNavBar()->setRightButtonText("连接");
    addWidget(listMenu);

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

TalkScanPage::~TalkScanPage() {
}

void TalkScanPage::showPage() {
    UIPage::showPage();
    state = SC_SCAN;
    lastCount = -1;
    connRows = 0;
    lastLink = talkManager.getLinkState();   // 进页时同步(已连接进页不触发自动跳语音页)
    hasWaitMac = false;
    seenDropMs = talkManager.getLastDropMs();  // 进页前的旧掉线事件不弹窗
    hideIncoming();
    listMenu->bVisible = true;
    talkManager.begin();
    talkManager.startScan();
    refreshList();
}

void TalkScanPage::refreshList() {
    int n = talkManager.getPeerCount();
    if (n == lastCount) {
        // 数量未变仍需更新忙闲文字(低频, 由update节流调用)
    }
    lastCount = n;
    bool connected = talkManager.isPeerConnected();
    connRows = connected ? 1 : 0;
    int sel = listMenu->menuSel;
    listMenu->clear();
    if (connected) {
        char nm[TALK_DEVNAME_MAX + 1] = {0};
        talkManager.getPeerName(nm, sizeof(nm));
        char line[48];
        snprintf(line, sizeof(line), "%s[已连接]", nm[0] ? nm : "?");
        listMenu->addMenuItem(line);
    }
    if (n == 0 && !connected) {
        listMenu->addMenuItem(talkManager.getLinkState() == TALK_LINK_IDLE
                              ? "对讲未启动，请重试" : "搜索中...");
    } else {
        for (int i = 0; i < n; i++) {
            TalkPeerInfo p;
            if (!talkManager.getPeer(i, p)) continue;
            // 已连接的对端在扫描列表中跳过(首行已展示已连接项)
            if (connected) {
                char nm[TALK_DEVNAME_MAX + 1] = {0};
                talkManager.getPeerName(nm, sizeof(nm));
                if (nm[0] && strcmp(p.name, nm) == 0) continue;
            }
            char line[48];
            snprintf(line, sizeof(line), "%s%s", p.name, p.busy ? "(忙)" : "");
            listMenu->addMenuItem(line);
        }
        // 除首行外无可连设备且未连接 → 提示搜索中
        if (listMenu->itemCount() == (size_t)connRows) {
            if (!connected) listMenu->addMenuItem("搜索中...");
        }
    }
    if (sel >= (int)listMenu->itemCount()) sel = listMenu->itemCount() - 1;
    if (sel < 0) sel = 0;
    listMenu->menuSel = sel;
    // 首行[已连接]设备选中时右键为"断开", 否则为"连接"
    listMenu->getNavBar()->setRightButtonText(
        (connRows > 0 && sel == 0 && connected) ? "断开" : "连接");
}

void TalkScanPage::showIncoming() {
    // 来电弹窗: 显示对端设备名(中文UTF8, 超长截断)
    char msg[64];
    String nm = talkManager.getIncomingName();
    if (nm.length() > 24) nm = nm.substring(0, 24);
    snprintf(msg, sizeof(msg), "%s\n请求对讲\n是否接受?", nm.c_str());
    incomingBox->setMessage(msg);
    listMenu->bVisible = false;
    incomingBox->show(MSGBOX_ANIME_ZOOM_CENTER, 300);
    incomingNav->bVisible = true;
}

void TalkScanPage::hideIncoming() {
    incomingBox->hide(false);
    incomingBox->bVisible = false;
    incomingNav->bVisible = false;
    listMenu->bVisible = true;
}

void TalkScanPage::showDropAlert() {
    // 对端掉线/离开提示: 复用来电弹窗, 无按钮2.5秒自动关闭
    state = SC_ALERT;
    bool lost = talkManager.getLinkDropReason() == TALK_DROP_LOST;
    incomingBox->setTitle("提示");
    incomingBox->setMessage(lost ? "连接已断开\n对方关机或超出距离"
                                 : "连接已断开\n对端已离开");
    incomingBox->bShowButtons = false;
    incomingNav->bVisible = false;
    listMenu->bVisible = false;
    incomingBox->cancelAutoClose();
    incomingBox->show(MSGBOX_ANIME_ZOOM_CENTER, 200);
    incomingBox->setAutoClose(2500);
    SoundAlertBeep(200);
}

void TalkScanPage::update() {
    if (incomingBox) incomingBox->update();
    TalkLinkState st = talkManager.getLinkState();
    // 掉线提示展示中: 等弹窗2.5秒自动关闭后恢复列表, 期间冻结所有状态跳转
    if (state == SC_ALERT) {
        lastLink = st;
        if (!incomingBox->isVisible()) {
            state = SC_SCAN;
            listMenu->bVisible = true;
            lastCount = -1;
            refreshList();
        }
        return;
    }
    // 来电到达(含本页已在INCOMING但对端重发/换人: 刷新名字)
    if (st == TALK_LINK_INCOMING && state != SC_INCOMING) {
        state = SC_INCOMING;
        showIncoming();
        SoundAlertBeep(150);   // 来电提示音
    }
    // 来电消失(对端取消/超时)
    if (state == SC_INCOMING && st == TALK_LINK_READY) {
        state = SC_SCAN;
        hideIncoming();
    }
    // 我方发起后对端接受 → 自动进语音页
    if (st == TALK_LINK_CONNECTED && lastLink != TALK_LINK_CONNECTED) {
        talkManager.stopScan();
        hideIncoming();   // 兜底(接受来电分支可能弹窗仍在)
        uiEngine.navigateTo(new TalkVoicePage());
        lastLink = st;
        return;
    }
    // 对端拒绝/超时 → 回扫描态
    if (state == SC_WAIT_ACCEPT && st == TALK_LINK_READY) {
        state = SC_SCAN;
        hasWaitMac = false;
    }
    // 对端离开/本端断开 → 首行[已连接]项消失, 刷新回扫描列表(保持在本页)
    if (st != lastLink && (st == TALK_LINK_READY || st == TALK_LINK_CONNECTED)) {
        lastCount = -1;   // 强制刷新首行
    }
    // 新掉线事件(对端关机/超距/正常离开): 扫描态弹一次自动关闭提示
    {
        unsigned long dropMs = talkManager.getLastDropMs();
        if (state == SC_SCAN && dropMs != 0 && dropMs != seenDropMs) {
            seenDropMs = dropMs;
            lastLink = st;
            showDropAlert();
            return;
        }
    }
    lastLink = st;
    if (state != SC_SCAN) return;
    // 右键文字跟随选中行(首行=已连接设备→断开, 其余=连接)
    {
        bool connected = talkManager.isPeerConnected();
        const char* want = (connRows > 0 && listMenu->menuSel == 0 && connected) ? "断开" : "连接";
        listMenu->getNavBar()->setRightButtonText(want);
    }
    // 1秒刷新一次列表
    if (millis() - lastRefreshMs > 1000) {
        lastRefreshMs = millis();
        refreshList();
    }
}

void TalkScanPage::render(U8G2* u8g2) {
    UIPage::render(u8g2);
    update();
}

void TalkScanPage::onButtonBack(void* context) {
    if (state == SC_ALERT) return;   // 掉线提示自动关闭, 按键不响应
    if (state == SC_INCOMING) {
        incomingNav->showLeftBlink(1, 80, 80, [this]() {
            talkManager.acceptIncoming(false);
            state = SC_SCAN;
            hideIncoming();
        });
        return;
    }
    if (state == SC_WAIT_ACCEPT) return;   // 等确认时BACK忽略(等超时/对端应答)
    listMenu->getNavBar()->showLeftBlink(1, 80, 80, [this]() {
        // 退出连接页: 停主动扫描(关闭可发现广播, 心跳降为2秒低频),
        // 已建立的连接保持(回菜单仍可进对讲/聊天)
        talkManager.stopScan();
        uiEngine.navigateBack();
    });
}

void TalkScanPage::onButtonEnter(void* context) {
    if (state == SC_ALERT) return;   // 掉线提示自动关闭, 按键不响应
    if (state == SC_INCOMING) {
        incomingNav->showRightBlink(1, 80, 80, [this]() {
            talkManager.acceptIncoming(true);
            // 连接成功后update()会检测到CONNECTED并进语音页
        });
        return;
    }
    if (state == SC_WAIT_ACCEPT) return;
    int sel = listMenu->menuSel;
    // 首行是已连接设备 → 断开(右侧按钮, 不断射频, 保持可回连)
    if (connRows > 0 && sel == 0 && talkManager.isPeerConnected()) {
        listMenu->getNavBar()->showRightBlink(1, 80, 80, [this]() {
            talkManager.disconnect();
            lastCount = -1;
            state = SC_SCAN;
            refreshList();
        });
        return;
    }
    if (talkManager.getPeerCount() == 0) return;
    int peerIdx = sel - connRows;   // 跳过首行[已连接]项
    TalkPeerInfo p;
    if (!talkManager.getPeer(peerIdx, p)) return;
    if (p.busy) return;   // 忙设备不发起
    memcpy(waitMac, p.mac, 6);
    hasWaitMac = true;
    listMenu->getNavBar()->showRightBlink(1, 80, 80, [this]() {
        talkManager.stopScan();
        talkManager.connectPeer(waitMac, (uint8_t)talkManager.getMode());
        state = SC_WAIT_ACCEPT;
    });
}

void TalkScanPage::onButtonMenu(void* context) {
    if (state != SC_SCAN) return;
    listMenu->moveDown();
}

void TalkScanPage::onButton2(void* context) {
    if (state != SC_SCAN) return;
    listMenu->moveUp();
}

void TalkScanPage::onButton5(void* context) {
    onButtonEnter(context);
}

void TalkScanPage::onButton8(void* context) {
    if (state != SC_SCAN) return;
    listMenu->moveDown();
}
