/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// TalkChatPage.cpp - 文字聊天页实现(终端式历史+按需出现的输入栏)
// 布局128x64: 无标题栏; 浏览态y=0..48共4行消息, 输入态3行消息+分隔线36+输入行(基线46)
//             底部UINavBar(49..64): 左=返回 中=按5输入/请输入 右=发送
// 消息显示: "收:内容"/"发:内容"靠左, 超宽自动折行(续行顶格), 最新在底部(终端式)
#include "TalkChatPage.h"
#include "TalkScanPage.h"
#include "../../GUI/UIEngine.h"
#include "../../Services/TalkManager.h"
#include "../../Drivers/AudioPlayer.h"

extern UIEngine uiEngine;

#define TC_FONT_CN       u8g2_font_wqy12_t_gb2312
#define TC_LINE_H        11                        // 消息行高
#define TC_BROWSE_LINES  4                         // 浏览态可见消息行数
#define TC_INPUT_LINES   3                         // 输入态可见消息行数(输入栏占一行)
#define TC_INPUT_SEP_Y   36                        // 输入栏上分隔线y

// UTF-8字符长度判定
static size_t tcCharLen(const String& s, size_t k) {
    uint8_t c = (uint8_t)s[k];
    if ((c & 0x80) == 0x00) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

TalkChatPage::TalkChatPage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT) {
    input = new UIInput(0, 0, 128, 50);
    input->setTitle("");
    input->setTextAlign(INPUT_LEFT);
    input->bVisible = false;
    addWidget(input);

    navBar = new UINavBar(0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, 15);
    navBar->setLeftButtonText("返回");
    navBar->setRightButtonText("发送");
    navBar->bVisible = true;
    addWidget(navBar);
}

TalkChatPage::~TalkChatPage() {
}

void TalkChatPage::showPage() {
    UIPage::showPage();
    talkManager.begin();
    mode = MODE_BROWSE;
    viewOffset = 0;
    lastTextMs = talkManager.getLastTextMs();
    lastLink = talkManager.isPeerConnected();
    talkManager.markRead();
    syncMiddleHint();
}

void TalkChatPage::syncMiddleHint() {
    // 浏览态中间提示: 已连接→按5输入 / 未连接→未连接; 输入态→请输入
    if (mode == MODE_INPUT) navBar->setMiddleButtonText("请输入");
    else navBar->setMiddleButtonText(
        talkManager.isPeerConnected() ? "按5输入" : "未连接");
}

void TalkChatPage::enterInput(int feedKey) {
    mode = MODE_INPUT;
    syncMiddleHint();
    if (feedKey >= 1 && feedKey <= 9) input->onButtonPress(feedKey);
}

void TalkChatPage::exitToBrowse(bool keepDraft) {
    mode = MODE_BROWSE;
    syncMiddleHint();
    if (!keepDraft) input->clearText();
}

void TalkChatPage::sendCurrent() {
    String t = input->getText();
    t.trim();
    if (t.length() == 0) return;
    if (!talkManager.isPeerConnected()) return;
    size_t maxB = TALK_TEXT_FRAG_BYTES * TALK_TEXT_MAX_FRAGS;
    if (t.length() > maxB) t = t.substring(0, maxB);
    if (talkManager.sendText(t)) {
        SoundAlertBeep(60);
        viewOffset = 0;   // 跳到底部看自己刚发的
        exitToBrowse(false);   // 发送完成→输入栏收起
    }
}

// 历史折行成行列表(旧→新, 首行带收:或发:前缀, 续行顶格全宽)
// 签名缓存(条数+最新一条时间戳): 历史未变则不重算, 避免每帧String抖动
void TalkChatPage::rebuildLines(U8G2* u8g2) {
    int n = talkManager.getChatCount();
    unsigned long sigMs = 0;
    if (n > 0) {
        TalkChatMsg m0;
        if (talkManager.getChat(0, m0)) sigMs = m0.timeMs;
    }
    if (n == linesSigCount && sigMs == linesSigMs) return;
    linesSigCount = n;
    linesSigMs = sigMs;
    chatLines.clear();
    if (n == 0) { lineTotal = 0; return; }
    u8g2->setFont(TC_FONT_CN);
    const int prefixW = u8g2->getUTF8Width("收:");          // 与"发："同宽
    const int availFirst = SCREEN_WIDTH - prefixW - 2;      // 首行: 前缀+内容
    const int availCont = SCREEN_WIDTH - 2;                 // 续行: 顶格全宽
    for (int i = n - 1; i >= 0; i--) {                       // 旧→新
        TalkChatMsg m;
        if (!talkManager.getChat(i, m)) continue;
        String cur = m.mine ? "发:" : "收:";
        int w = prefixW;
        bool first = true;
        String body = String(m.text);
        for (size_t k = 0; k < body.length(); ) {
            size_t cl = tcCharLen(body, k);
            String ch = body.substring(k, k + cl);
            int cw = u8g2->getUTF8Width(ch.c_str());
            int avail = first ? availFirst : availCont;
            if (w + cw > avail) {
                chatLines.push_back(cur);
                cur = "";
                w = 0;
                first = false;
            }
            cur += ch;
            w += cw;
            k += cl;
        }
        if (cur.length() > 0) chatLines.push_back(cur);
    }
    lineTotal = (int)chatLines.size();
}

void TalkChatPage::drawHistory(U8G2* u8g2) {
    u8g2->setFont(TC_FONT_CN);
    if (talkManager.getChatCount() == 0) {
        const char* msg = talkManager.isPeerConnected() ? "无消息,按5输入" : "未连接,按5连接设备";
        int w = u8g2->getUTF8Width(msg);
        u8g2->drawUTF8((SCREEN_WIDTH - w) / 2, 27, msg);
        return;
    }
    rebuildLines(u8g2);
    // 视窗: 底部对齐最新一条, viewOffset向上滚动历史(终端式)
    const int vis = (mode == MODE_INPUT) ? TC_INPUT_LINES : TC_BROWSE_LINES;
    if (viewOffset > lineTotal - vis) viewOffset = lineTotal - vis;
    if (viewOffset < 0) viewOffset = 0;
    int start = lineTotal - vis - viewOffset;
    if (start < 0) start = 0;
    for (int k = 0; k < vis && start + k < lineTotal; k++) {
        u8g2->drawUTF8(0, 10 + k * TC_LINE_H, chatLines[start + k].c_str());
    }
    // 右侧滚动条(仅内容超出可视区时): 1px轨道+2px滑块, 指示当前位置
    if (lineTotal > vis) {
        const int trTop = 1;
        const int trBot = (mode == MODE_INPUT) ? 34 : 47;   // 跟随消息区高度
        const int trH = trBot - trTop + 1;
        int thH = (trH * vis) / lineTotal;
        if (thH < 3) thH = 3;
        if (thH > trH) thH = trH;
        int ty = trTop + ((trH - thH) * start) / (lineTotal - vis);
        u8g2->drawVLine(SCREEN_WIDTH - 2, trTop, trH);      // 轨道
        u8g2->drawBox(SCREEN_WIDTH - 2, ty, 2, thH);        // 滑块
    }
}

void TalkChatPage::drawInput(U8G2* u8g2) {
    u8g2->setFont(TC_FONT_CN);
    u8g2->drawHLine(0, TC_INPUT_SEP_Y, SCREEN_WIDTH);
    // 只显示尾部(最近输入的内容); 前缀">>"即可(底部已有"请输入"提示)
    String t = input->getText();
    String prefix = ">>";
    int prefixW = u8g2->getUTF8Width(prefix.c_str());
    int avail = SCREEN_WIDTH - prefixW - u8g2->getUTF8Width("_");
    String fit;
    int w = 0;
    int count = 0;
    for (int k = (int)t.length() - 1; k >= 0; ) {
        int s = k;
        while (s > 0 && ((uint8_t)t[s] & 0xC0) == 0x80) s--;
        String ch = t.substring(s, k + 1);
        int cw = u8g2->getUTF8Width(ch.c_str());
        if (w + cw > avail || count >= 40) break;
        fit = ch + fit;
        w += cw;
        count++;
        k = s - 1;
    }
    String line = prefix + fit;
    static bool cur = false;
    static unsigned long lastMs = 0;
    if (millis() - lastMs > 500) { lastMs = millis(); cur = !cur; }
    if (cur) line += "_";
    u8g2->drawUTF8(0, 46, line.c_str());
}

void TalkChatPage::update() {
    input->update();
    if (talkManager.hasUnread()) {
        talkManager.markRead();
        viewOffset = 0;   // 新消息→跳到底部
        SoundAlertBeep(100);
    }
    lastTextMs = talkManager.getLastTextMs();
    bool link = talkManager.isPeerConnected();
    if (link != lastLink) {
        lastLink = link;
        viewOffset = 0;
        syncMiddleHint();
    }
}

void TalkChatPage::render(U8G2* u8g2) {
    UIPage::render(u8g2);
    update();
    drawHistory(u8g2);
    if (mode == MODE_INPUT) drawInput(u8g2);   // 输入栏仅输入模式绘制
}

void TalkChatPage::onButtonBack(void* context) {
    if (mode == MODE_INPUT) { exitToBrowse(true); return; }  // 输入中BACK=收起输入栏(保留草稿)
    navBar->showLeftBlink(1, 80, 80, [this]() {
        uiEngine.navigateBack();
    });
}

void TalkChatPage::onButtonEnter(void* context) {
    navBar->showRightBlink(1, 80, 80, [this]() {
        if (!talkManager.isPeerConnected()) {
            uiEngine.navigateTo(new TalkScanPage());
            return;
        }
        if (mode == MODE_BROWSE) {
            if (input->getText().length() > 0) sendCurrent();  // 有草稿则发送
            else enterInput();  // 空则展开输入栏(与5键等效)
        } else {
            sendCurrent();  // 输入模式ENTER=发送并收起输入栏
        }
    });
}

void TalkChatPage::onButtonMenu(void* context) {
    if (mode == MODE_BROWSE) { enterInput(); return; }  // 浏览MENU=展开输入栏
    input->deleteChar();
}

void TalkChatPage::onButton1(void* context) { if (mode == MODE_BROWSE) enterInput(1); else input->onButtonPress(1); }
void TalkChatPage::onButton3(void* context) { if (mode == MODE_BROWSE) enterInput(3); else input->onButtonPress(3); }
void TalkChatPage::onButton4(void* context) {
    if (mode == MODE_BROWSE) {   // 4=向上整页翻(更旧)
        viewOffset += TC_BROWSE_LINES;
        return;
    }
    input->onButtonPress(4);
}
void TalkChatPage::onButton6(void* context) {
    if (mode == MODE_BROWSE) {   // 6=向下整页翻(更新)
        viewOffset -= TC_BROWSE_LINES;
        return;
    }
    input->onButtonPress(6);
}
void TalkChatPage::onButton5(void* context) {
    if (mode == MODE_BROWSE) {
        if (talkManager.isPeerConnected()) enterInput();   // 按5展开输入栏
        else navBar->showRightBlink(1, 80, 80, [this]() {   // 未连接→去连接设备页
            uiEngine.navigateTo(new TalkScanPage());
        });
        return;
    }
    input->onButtonPress(5);
}
void TalkChatPage::onButton7(void* context) {
    if (mode == MODE_BROWSE) {
        if (talkManager.isPeerConnected()) {
            if (talkManager.sendQuick(1)) SoundAlertBeep(60);
        }
        return;
    }
    input->onButtonPress(7);
}
void TalkChatPage::onButton9(void* context) {
    if (mode == MODE_BROWSE) {
        if (talkManager.isPeerConnected()) {
            if (talkManager.sendQuick(0)) SoundAlertBeep(60);
        }
        return;
    }
    input->onButtonPress(9);
}
void TalkChatPage::onButton2(void* context) {
    if (mode == MODE_BROWSE) {   // 2=向上单行滚动(更旧)
        viewOffset++;
        return;
    }
    input->onButtonPress(2);
}
void TalkChatPage::onButton8(void* context) {
    if (mode == MODE_BROWSE) {   // 8=向下单行滚动(更新)
        viewOffset--;
        return;
    }
    input->onButtonPress(8);
}
