/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// TalkSettingPage.cpp - 对讲设置页实现(NVS持久化经TalkManager直写Preferences)
#include "TalkSettingPage.h"
#include "../../GUI/UIEngine.h"
#include "../../Services/TalkManager.h"
#include "../../Services/DataStore.h"
#include <Preferences.h>
#include "../../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "TALKCFG"

extern UIEngine uiEngine;
extern DataStore dataStore;

TalkSettingPage::TalkSettingPage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT) {
    titleLabel = new UILabel();
    titleLabel->x = 0; titleLabel->y = 0;
    titleLabel->width = SCREEN_WIDTH; titleLabel->height = 12;
    titleLabel->label = "对讲设置";
    titleLabel->textAlign = CENTER; titleLabel->verticalAlign = MIDDLE;
    addWidget(titleLabel);

    nameLabel = new UILabel();
    nameLabel->x = 0; nameLabel->y = 12; nameLabel->width = 40; nameLabel->height = 12;
    nameLabel->label = "设备名:";
    nameLabel->textFont = u8g2_font_wqy12_t_gb2312;
    addWidget(nameLabel);
    nameShow = new UISelectValue();
    nameShow->x = 42; nameShow->y = 12; nameShow->width = 86; nameShow->height = 12;
    nameShow->textFont = u8g2_font_wqy12_t_gb2312;
    addWidget(nameShow);

    chLabel = new UILabel();
    chLabel->x = 0; chLabel->y = 24; chLabel->width = 40; chLabel->height = 12;
    chLabel->label = "信道:";
    chLabel->textFont = u8g2_font_wqy12_t_gb2312;
    addWidget(chLabel);
    chShow = new UISelectValue();
    chShow->x = 42; chShow->y = 24; chShow->width = 86; chShow->height = 12;
    chShow->textFont = u8g2_font_wqy12_t_gb2312;
    addWidget(chShow);

    modeLabel = new UILabel();
    modeLabel->x = 0; modeLabel->y = 36; modeLabel->width = 40; modeLabel->height = 12;
    modeLabel->label = "模式:";
    modeLabel->textFont = u8g2_font_wqy12_t_gb2312;
    addWidget(modeLabel);
    modeShow = new UISelectValue();
    modeShow->x = 42; modeShow->y = 36; modeShow->width = 86; modeShow->height = 12;
    modeShow->textFont = u8g2_font_wqy12_t_gb2312;
    addWidget(modeShow);

    nameInput = new UIInput(0, 0, 128, 50);
    nameInput->setTitle("设备名输入");
    nameInput->bVisible = false;
    addWidget(nameInput);

    navBar = new UINavBar(0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, 15);
    navBar->setLeftButtonText("返回");
    navBar->setRightButtonText("保存");
    addWidget(navBar);

    nameNavBar = new UINavBar(0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, 15);
    nameNavBar->setLeftButtonText("返回");
    nameNavBar->setMiddleButtonText("删除");
    nameNavBar->setRightButtonText("确定");
    nameNavBar->bVisible = false;
    addWidget(nameNavBar);
}

TalkSettingPage::~TalkSettingPage() {
}

void TalkSettingPage::showPage() {
    UIPage::showPage();
    talkManager.begin();
    // 读NVS(TalkManager.init已读过, 此处以管理器快照为准, NVS仅做显示兜底)
    Preferences pf;
    String nm = talkManager.getDeviceName();
    int ch = talkManager.getChannel();
    int dm = (talkManager.getMode() == TALK_MODE_FULL) ? 1 : 0;
    if (pf.begin("RadioData", true)) {
        String saved = pf.getString("TALK_NAME", nm);
        // 按字节截断(中文UTF-8尾部可能截断, 显示端容错)
        if (saved.length() > TALK_DEVNAME_MAX) saved = saved.substring(0, TALK_DEVNAME_MAX);
        nm = saved;
        ch = pf.getInt("TALK_CH", ch);
        dm = pf.getInt("TALK_MODE", dm);
        pf.end();
    }
    nameInput->setText(nm);
    channel = (ch >= 1 && ch <= 13) ? ch : 1;
    defMode = (dm == 1) ? 1 : 0;
    // 进页即显示当前值(此前只在保存时赋value→首次进入三个框全空)
    nameShow->value = nm;
    chShow->value = String("CH") + channel;
    modeShow->value = (defMode == 0) ? "PTT" : "FULL";
    field = F_NAME;
    state = ST_LIST;
    setListVisible(true);
    refreshShow();
}

void TalkSettingPage::refreshShow() {
    nameShow->bSelected = (field == F_NAME);
    chShow->bSelected = (field == F_CH);
    modeShow->bSelected = (field == F_MODE);
}

void TalkSettingPage::setListVisible(bool v) {
    titleLabel->bVisible = v;
    nameLabel->bVisible = v;
    nameShow->bVisible = v;
    chLabel->bVisible = v;
    chShow->bVisible = v;
    modeLabel->bVisible = v;
    modeShow->bVisible = v;
    navBar->bVisible = v;
    nameInput->bVisible = !v;
    nameNavBar->bVisible = !v;
}

void TalkSettingPage::enterNameEdit() {
    state = ST_NAME_EDIT;
    setListVisible(false);
}

void TalkSettingPage::exitNameEdit(bool save) {
    if (!save) {
        // 放弃本次输入,恢复显示用旧值由refreshShow从nameInput取,需外部已处理;
        // 这里直接返回列表,showPage重进时会重读NVS
    }
    state = ST_LIST;
    setListVisible(true);
    refreshShow();
}

void TalkSettingPage::adjustValue(int dir) {
    if (field == F_CH) {
        channel += dir;
        if (channel < 1) channel = 13;
        if (channel > 13) channel = 1;
        chShow->value = String("CH") + channel;   // 同步显示值(否则按4/6看不到变化)
    } else if (field == F_MODE) {
        defMode = 1 - defMode;
        modeShow->value = (defMode == 0) ? "PTT" : "FULL";
    }
    refreshShow();
}

void TalkSettingPage::saveAndReturn() {
    String nm = nameInput->getText();
    nm.trim();
    if (nm.length() == 0) nm = "MYNOVA";
    if (nm.length() > 12) nm = nm.substring(0, 12);
    nameShow->value = nm;
    chShow->value = String("CH") + channel;
    modeShow->value = (defMode == 0) ? "PTT" : "FULL";
    refreshShow();
    talkManager.setDeviceName(nm);
    talkManager.setChannel((uint8_t)channel);
    // 默认模式只改本端(不发CTRL打扰对端; 通话中切换走语音页4键)
    // requestMode会单播CTRL给已连接对端, 设置页保存不应触发 → 用持久化+下次生效
    DataStore::StorageGuard storage(dataStore);
    Preferences pf;
    if (storage && pf.begin(DataStore::SettingsNamespace, false)) {
        pf.putString("TALK_NAME", nm);
        pf.putInt("TALK_CH", channel);
        pf.putInt("TALK_MODE", defMode);
        pf.end();
    }
    LOG_I("TalkSetting: 保存 名=%s CH=%d 默认模式=%d(本端即时生效, 对端不受扰)",
          nm.c_str(), channel, defMode);
    uiEngine.navigateBack();
}

void TalkSettingPage::update() {
    nameInput->update();
}

void TalkSettingPage::render(U8G2* u8g2) {
    UIPage::render(u8g2);
    update();
}

void TalkSettingPage::onButtonBack(void* context) {
    if (state == ST_NAME_EDIT) {
        // 名称编辑页返回:放弃本次输入,恢复NVS旧值
        Preferences pf;
        String nm = talkManager.getDeviceName();
        if (pf.begin("RadioData", true)) { nm = pf.getString("TALK_NAME", nm); pf.end(); }
        nameInput->setText(nm);
        nameNavBar->showLeftBlink(1, 80, 80, [this]() { exitNameEdit(false); });
        return;
    }
    navBar->showLeftBlink(1, 80, 80, [this]() {
        uiEngine.navigateBack();
    });
}

void TalkSettingPage::onButtonEnter(void* context) {
    if (state == ST_NAME_EDIT) {
        nameNavBar->showRightBlink(1, 80, 80, [this]() { exitNameEdit(true); });
        return;
    }
    if (field == F_NAME) { enterNameEdit(); return; }
    navBar->showRightBlink(1, 80, 80, [this]() {
        saveAndReturn();
    });
}

void TalkSettingPage::onButtonMenu(void* context) {
    if (state == ST_NAME_EDIT) { nameInput->deleteChar(); return; }
}

void TalkSettingPage::onButton2(void* context) {
    if (state == ST_NAME_EDIT) { nameInput->onButtonPress(2); return; }
    field--; if (field < 0) field = F_COUNT - 1;
    refreshShow();
}

void TalkSettingPage::onButton8(void* context) {
    if (state == ST_NAME_EDIT) { nameInput->onButtonPress(8); return; }
    field++; if (field >= F_COUNT) field = 0;
    refreshShow();
}

void TalkSettingPage::onButton4(void* context) {
    if (state == ST_NAME_EDIT) { nameInput->onButtonPress(4); return; }
    if (field == F_NAME) { enterNameEdit(); return; }   // 名字行4/6=进入编辑(同5键)
    adjustValue(-1);
}

void TalkSettingPage::onButton6(void* context) {
    if (state == ST_NAME_EDIT) { nameInput->onButtonPress(6); return; }
    if (field == F_NAME) { enterNameEdit(); return; }   // 名字行4/6=进入编辑(同5键)
    adjustValue(+1);
}

void TalkSettingPage::onButton5(void* context) {
    if (state == ST_NAME_EDIT) { nameInput->onButtonPress(5); return; }
    if (field == F_NAME) { enterNameEdit(); return; }
    onButtonEnter(context);
}

void TalkSettingPage::onButton1(void* context) { if (state == ST_NAME_EDIT) nameInput->onButtonPress(1); else if (field == F_NAME) enterNameEdit(); }
void TalkSettingPage::onButton3(void* context) { if (state == ST_NAME_EDIT) nameInput->onButtonPress(3); else if (field == F_NAME) enterNameEdit(); }
void TalkSettingPage::onButton7(void* context) { if (state == ST_NAME_EDIT) nameInput->onButtonPress(7); else if (field == F_NAME) enterNameEdit(); }
void TalkSettingPage::onButton9(void* context) { if (state == ST_NAME_EDIT) nameInput->onButtonPress(9); else if (field == F_NAME) enterNameEdit(); }
