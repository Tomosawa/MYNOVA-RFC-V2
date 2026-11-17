/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

// SendDataPage.cpp - 发送模式页面（动态网格布局）
// IRLearnPage 之后统一：4行×2列网格，值长内容独占整行，状态小字紧跟前缀。
#include "SendDataPage.h"
#include "../../GUI/UIEngine.h"
#include "../../GUI/Animation/MessageBoxAnimation.h"
#include "../../GUI/Animation/AnimationEngine.h"
#include "../../GUI/GUIRender.h"
#include "../../Services/DataStore.h"
#include "../../Drivers/RadioHelper.h"
#include "../../Drivers/IRHelper.h"

extern UIEngine uiEngine;
extern RadioHelper radioHelper;
extern IRHelper irHelper;
extern DataStore dataStore;

#define SEND_GRID_COL2_X   68
#define SEND_GRID_COL1_W   66
#define SEND_GRID_COL2_W   60

SendDataPage::SendDataPage(SendFilterMode filterMode) : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT),
    currentState(STATE_SELECT),
    filter(filterMode),
    slotListCount(0),
    bAnimating(false),
    selectedDataIndex(0),
    irViewFrame(0) {
    statusContentLabel = nullptr;
    freqLabel = nullptr;
    dataLabel = nullptr;
    bitLengthLabel = nullptr;
    pulseLengthLabel = nullptr;
    typeLabel = nullptr;
    nameLabel = nullptr;
    for (int i = 0; i < 4; i++) {
        rows[i][0] = nullptr;
        rows[i][1] = nullptr;
    }
    initLayout();
}

SendDataPage::~SendDataPage() {
    if (rawSending) radioHelper.CancelRawTransmit();
}

void SendDataPage::initLayout() {
    dataListMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    dataListMenu->titleText = "选择发送数据";
    dataListMenu->bShowBtnTips = true;
    dataListMenu->bShowBorder = false;
    dataListMenu->bShowTitle = false;
    dataListMenu->bVisible = true;
    dataListMenu->getNavBar()->setLeftButtonText("返回");
    dataListMenu->getNavBar()->setRightButtonText("选择");
    auto addEntries = [&](SignalKind k, int count, const char* prefix){
        for (int i = 1; i <= count; i++) {
            SignalEntry entry = dataStore.ReadSignal(k, i);
            if (entry.name.length() == 0) continue;
            String itemName = String(i) + ".";
            itemName += prefix;
            itemName += entry.name;
            dataListMenu->addMenuItem(itemName);
            slotList[slotListCount++] = KindIndexToSlot(k, i);
        }
    };
    if (filter == FILTER_RF) addEntries(SIG_RF, RF_SLOT_COUNT, "[RF]");
    else if (filter == FILTER_IR) addEntries(SIG_IR, IR_SLOT_COUNT, "[IR]");
    else {
        addEntries(SIG_RF, RF_SLOT_COUNT, "[RF]");
        addEntries(SIG_IR, IR_SLOT_COUNT, "[IR]");
    }
    if (slotListCount == 0) {
        dataListMenu->addMenuItem("-- 无数据 --");
        slotList[0] = -1;
        slotListCount = 1;
    }
    addWidget(dataListMenu);

    // 名称（详情页顶部，跨列显示）
    nameLabel = new UILabel();
    nameLabel->x = 0;
    nameLabel->y = 0;
    nameLabel->width = SCREEN_WIDTH;
    nameLabel->height = 10;
    nameLabel->label = "名称: --";
    nameLabel->textFont = u8g2_font_wqy12_t_gb2312;
    nameLabel->textAlign = LEFT;
    nameLabel->verticalAlign = MIDDLE;
    nameLabel->bVisible = false;
    addWidget(nameLabel);

    // 4行网格（y=12起，模仿学习页后的统一排布；名称占顶行，网格从第二行起5行？改为4行内容区+名称顶行）
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 2; j++) {
            UILabel* lb = new UILabel();
            lb->x = (j == 0) ? 0 : SEND_GRID_COL2_X;
            lb->y = 12 + i * 10;
            lb->width = (j == 0) ? SEND_GRID_COL1_W : SEND_GRID_COL2_W;
            lb->height = 10;
            lb->label = "";
            lb->textFont = u8g2_font_wqy12_t_gb2312;
            lb->textAlign = LEFT;
            lb->verticalAlign = MIDDLE;
            lb->bVisible = false;
            addWidget(lb);
            rows[i][j] = lb;
        }
    }
    statusContentLabel = new UILabel();
    statusContentLabel->x = 32;
    statusContentLabel->y = 42;
    statusContentLabel->width = SCREEN_WIDTH - 32;
    statusContentLabel->height = 10;
    statusContentLabel->label = "";
    statusContentLabel->textFont = u8g2_font_4x6_tf;
    statusContentLabel->textAlign = LEFT;
    statusContentLabel->verticalAlign = MIDDLE;
    statusContentLabel->bVisible = false;
    addWidget(statusContentLabel);

    detailNavBar = new UINavBar(0, 52, SCREEN_WIDTH, 12);
    detailNavBar->setMargin(0, 0);
    detailNavBar->bVisible = false;
    detailNavBar->setLeftButtonText("返回");
    detailNavBar->setMiddleButtonText("设快捷");
    detailNavBar->setRightButtonText("发送");
    addWidget(detailNavBar);
    
    // 主页选择（设快捷前置）
    quickProfileMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    quickProfileMenu->bShowBtnTips = true; quickProfileMenu->bShowBorder=false; quickProfileMenu->bShowTitle=false;
    quickProfileMenu->bVisible=false;
    quickProfileMenu->getNavBar()->setLeftButtonText("返回");
    quickProfileMenu->getNavBar()->setRightButtonText("选择");
    addWidget(quickProfileMenu);

    messageBox = new UIMessageBox(-1, -1, 100, 50);
    messageBox->setDefaultPosition(SCREEN_WIDTH, SCREEN_HEIGHT, 15);
    messageBox->setMessage("该位置无数据！");
    messageBox->setTitle("提示");
    messageBox->bShowButtons = false;
    addWidget(messageBox);
    
    quickSetBox = new UIMessageBox(-1, -1, 100, 50);
    quickSetBox->setDefaultPosition(SCREEN_WIDTH, SCREEN_HEIGHT, 15);
    quickSetBox->setMessage("请按1-9键设置");
    quickSetBox->setTitle("设置快捷键");
    quickSetBox->bShowButtons = false;
    addWidget(quickSetBox);
    
    quickSetNavBar = new UINavBar(0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, 15);
    quickSetNavBar->setLeftButtonText("返回");
    quickSetNavBar->bVisible = false;
    addWidget(quickSetNavBar);
}

void SendDataPage::update() {
    if (!rawSending) return;
    RawTransmitState result = radioHelper.GetRawTransmitState();
    if (result == RawTransmitState::Sending) return;
    rawSending = false;
    bAnimating = false;
    detailNavBar->setLeftButtonText("返回");
    detailNavBar->setRightButtonText("发送");
    detailNavBar->setMiddleButtonText(result == RawTransmitState::Sent ? "发送成功" :
                                    result == RawTransmitState::Cancelled ? "已停止" : "发送失败");
}

void SendDataPage::render(U8G2* u8g2) {
    UIPage::render(u8g2);
}

// ── 网格助手
void SendDataPage::rowSplit(int row, const String& left, const String& right) {
    if (row == 3) statusContentLabel->bVisible = false;
    rows[row][0]->label = left;
    rows[row][0]->bVisible = true;
    rows[row][1]->label = right;
    rows[row][1]->textFont = u8g2_font_wqy12_t_gb2312;
    rows[row][1]->bVisible = true;
}
void SendDataPage::rowFull(int row, const String& text) {
    if (row == 3) statusContentLabel->bVisible = false;
    rows[row][0]->label = text;
    rows[row][0]->bVisible = true;
    rows[row][1]->bVisible = false;
}
void SendDataPage::rowPrefixSmall(const String& prefix, const String& content) {
    rows[3][0]->label = prefix;
    rows[3][0]->bVisible = true;
    rows[3][1]->bVisible = false;
    statusContentLabel->label = content;
    statusContentLabel->bVisible = true;
}
void SendDataPage::hideDetailGrid() {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 2; j++) rows[i][j]->bVisible = false;
    }
    statusContentLabel->bVisible = false;
}

String SendDataPage::irFrameValueHex(uint8_t fi) {
    const IRSignalMeta& m = currentData.ir;
    uint64_t v = m.frameValue[fi];
    char buf[17];
    if (m.acStateDesc[0] != '\0') {
        for (int i = 0; i < 8; i++) snprintf(&buf[i * 2], 3, "%02X", (unsigned)((v >> (8 * i)) & 0xFF));
        buf[16] = '\0';
    } else if (m.frameProto[fi] == IR_PROTO_BYTE_UNKNOWN) {
        snprintf(buf, sizeof(buf), "%08llX", (unsigned long long)(v & 0xFFFFFFFFULL));
    } else {
        snprintf(buf, sizeof(buf), "%llX", (unsigned long long)v);
    }
    return String(buf);
}

void SendDataPage::showDataDetail(int slotIndex) {
    if (slotIndex < 1 || slotIndex > SLOT_IR_OFFSET + IR_SLOT_COUNT) {
        messageBox->show(MSGBOX_ANIME_BOUNCE_IN, 400);
        messageBox->setAutoClose(3000);
        dataListMenu->bVisible = false;
        currentState = STATE_SENDING;
        return;
    }
    SignalEntry entry;
    if (SlotIsIR(slotIndex)) entry = dataStore.ReadSignal(SIG_IR, SlotToIndex(slotIndex));
    else entry = dataStore.ReadSignal(SIG_RF, slotIndex);
    if (entry.name.length() == 0) {
        // 兼容旧统一索引残留
        entry = dataStore.ReadSignal(slotIndex);
    }
    if (entry.name.length() == 0) {
        messageBox->show(MSGBOX_ANIME_BOUNCE_IN, 400);
        messageBox->setAutoClose(3000);
        dataListMenu->bVisible = false;
        currentState = STATE_SENDING;
        return;
    }
    dataListMenu->bVisible = false;
    currentState = STATE_DETAIL;
    selectedDataIndex = slotIndex;
    currentData = entry;
    nameLabel->label = "名称:" + entry.name;
    nameLabel->bVisible = true;
    hideDetailGrid();
    if (entry.kind == SIG_IR) {
        irViewFrame = 0;
        refreshIRDetailRows();
    } else {
        refreshRFDetailRows();
    }
    detailNavBar->bVisible = true;
}

void SendDataPage::refreshIRDetailRows() {
    // 红外详情保持原排布：名称顶行 + 4行网格
    nameLabel->bVisible = true;
    for (int i = 0; i < 4; i++) {
        rows[i][0]->x = 0;
        rows[i][0]->y = 12 + i * 10;
        rows[i][0]->width = SEND_GRID_COL1_W;
        rows[i][0]->height = 10;
        rows[i][1]->x = SEND_GRID_COL2_X;
        rows[i][1]->y = 12 + i * 10;
        rows[i][1]->width = SEND_GRID_COL2_W;
        rows[i][1]->height = 10;
    }
    const IRSignalMeta& m = currentData.ir;
    if (irViewFrame >= m.frameCount) irViewFrame = 0;
    uint8_t fi = irViewFrame;
    char buf[40];
    String proto = (m.frameProto[fi] == IR_PROTO_BYTE_UNKNOWN) ? "未识别" :
                   (m.protocolName[0] ? String(m.protocolName) : typeToString((decode_type_t)m.frameProto[fi]));
    if (proto.length() == 0) proto = "RAW";
    snprintf(buf, sizeof(buf), "帧:%u/%u", fi + 1, m.frameCount);
    rowSplit(0, "协议:" + proto, String(buf));
    rowFull(1, String("值:") + irFrameValueHex(fi));
    snprintf(buf, sizeof(buf), "位宽:%ubit", m.frameBits[fi]);
    String row2L(buf);
    snprintf(buf, sizeof(buf), "边沿:%u", m.frameLens[fi]);
    rowSplit(2, row2L, String(buf));
    if (m.acStateDesc[0] != '\0') {
        String desc = m.acStateDesc;
        if (desc.length() > 24) desc = desc.substring(0, 24) + "..";
        rowPrefixSmall("状态:", desc);
    } else if (m.frameProto[fi] != IR_PROTO_BYTE_UNKNOWN && (m.frameAddr[fi] != 0 || m.frameCmd[fi] != 0)) {
        rowSplit(3, String("地址:") + String(m.frameAddr[fi], HEX), String("命令:") + String(m.frameCmd[fi], HEX));
    } else if (fi > 0) {
        rowSplit(3, String("间隔:") + String(m.frameGapsMs[fi]) + "ms", String("时长:--"));
    } else {
        rowFull(3, String("边沿总数:") + String((unsigned)m.totalEdges));
    }
}

void SendDataPage::refreshRFDetailRows() {
    // 射频详情：无名称行；第1行频率+位宽，第2行脉宽，第3行协议，第4行数据
    nameLabel->bVisible = false;
    const RFSignalData& r = currentData.rf;
    for (int i = 0; i < 4; i++) {
        rows[i][0]->x = 0;
        rows[i][0]->y = i * 13;
        rows[i][0]->height = 12;
        rows[i][1]->y = i * 13;
        rows[i][1]->height = 12;
    }
    rows[0][0]->width = SEND_GRID_COL1_W;
    rows[0][1]->x = SEND_GRID_COL2_X;
    rows[0][1]->width = SEND_GRID_COL2_W;
    char buf[40];
    snprintf(buf, sizeof(buf), "%.2fM", r.frequency);
    if (r.rawFlag) {
        // RAW 录制信号：频率 / 段数 / 调制预设，发送读边车文件原样重放
        rowSplit(0, String("频率:") + String(buf), String("段数:") + (r.rawCount == 65535 ? String(">=") : String("")) + String(r.rawCount));
        // 预设短名：从 Flipper 名取最后一段（如 Ook270Async → AM 预设直接显示枚举短名需映射，
        // 这里显示完整名尾部，简洁起见裁剪前缀）
        String preset = r.rawPreset[0] ? String(r.rawPreset) : String("Ook650Async");
        int cut = preset.lastIndexOf("Preset");
        if (cut >= 0) preset = preset.substring(cut + 6);
        rowFull(1, String("调制:") + preset);
        rows[1][0]->width = SCREEN_WIDTH;
        rowFull(2, "协议: RAW 录制");
        rows[2][0]->width = SCREEN_WIDTH;
        rowFull(3, "原样回放（滚码可能仅一次有效）");
        rows[3][0]->width = SCREEN_WIDTH;
        detailNavBar->setMiddleButtonText("");
        return;
    }
    rowSplit(0, String("频率:") + String(buf), String("位宽:") + String(r.bitCount) + "bit");
    rowFull(1, String("脉宽:") + String(r.shortPulse) + "/" + String(r.longPulse));
    rows[1][0]->width = SCREEN_WIDTH;
    String proto = r.protocolName[0] ? String(r.protocolName) : "RAW";
    rowFull(2, String("协议:") + proto);
    rows[2][0]->width = SCREEN_WIDTH;
    String dataStr = formatHexData(r);
    if (r.bitCount > 40) dataStr += "..";
    rowFull(3, String("数据:") + dataStr);
    rows[3][0]->width = SCREEN_WIDTH;
    detailNavBar->setMiddleButtonText("");
}

void SendDataPage::cycleIRFrame(int delta) {
    if (currentData.kind != SIG_IR || currentData.ir.frameCount == 0) return;
    int next = (int)irViewFrame + delta;
    if (next < 0) next = currentData.ir.frameCount - 1;
    if (next >= currentData.ir.frameCount) next = 0;
    irViewFrame = (uint8_t)next;
    refreshIRDetailRows();
}

String SendDataPage::formatHexData(const RFSignalData& data) {
    uint16_t bytes = (data.bitCount + 7) / 8;
    if (bytes > 5) bytes = 5;   // 最多显示5字节（40位），超出截断加".." 
    String formattedStr = "";
    for (uint16_t i = 0; i < bytes; i++) {
        if (i > 0) formattedStr += " ";
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X", data.data[i]);
        formattedStr += buf;
    }
    if (formattedStr.length() == 0) formattedStr = "00";
    return formattedStr;
}

bool SendDataPage::sendCurrentSignal() {
    if (currentData.kind == SIG_RF) return radioHelper.SendData(currentData.rf);
    int irIdx = SlotIsIR(selectedDataIndex) ? SlotToIndex(selectedDataIndex) : selectedDataIndex;
    return irHelper.SendFromSlot(irIdx);
}

static bool quickKeyValidForLayout(uint8_t layout, int keyIndex) {
    if (keyIndex < 1 || keyIndex > 9) return false;
    return keyIndex <= HomeLayoutToKeyCount(layout);
}

void SendDataPage::enterQuickProfilePick() {
    quickProfileMenu->clear();
    quickProfileMenu->setShowRadio(true);
    auto list = dataStore.ListHomeProfiles();
    int active = dataStore.GetActiveHomeProfile();
    int activeIdx = -1;
    for (auto& p: list) {
        int n = HomeLayoutToKeyCount(p.layout);
        String suffix = (n == 1) ? " [单键]" : (" [" + String(n) + "键]");
        quickProfileMenu->addMenuItem(p.name + suffix);
        if (p.id == active) activeIdx = (int)quickProfileMenu->itemCount() - 1;
    }
    quickProfileId = -1;
    for (auto& p: list) if (p.id==active) { quickProfileId=p.id; break; }
    if (quickProfileId<0 && !list.empty()) quickProfileId=list[0].id;
    if (activeIdx >= 0) {
        int itemLines = quickProfileMenu->menuLines;
        if (quickProfileMenu->bShowTitle) itemLines--;
        if (quickProfileMenu->bShowBtnTips) itemLines--;
        int n = (int)list.size();
        if (n <= itemLines) { quickProfileMenu->menuStart = 0; quickProfileMenu->menuPos = activeIdx; }
        else { quickProfileMenu->menuStart = activeIdx; quickProfileMenu->menuPos = 0; if (activeIdx > n - itemLines) quickProfileMenu->menuStart = n - itemLines; }
        quickProfileMenu->menuSel = activeIdx;
    }
    quickProfileMenu->setRadioSelected(activeIdx);
    quickProfileMenu->bVisible=true;
    detailNavBar->setMiddleButtonText("");
    detailNavBar->bVisible=false;
    currentState = STATE_SET_QUICK_PROFILE;
}

void SendDataPage::enterQuickKeyPick() {
    quickProfileMenu->bVisible=false;
    quickSetBox->show(MSGBOX_ANIME_SLIDE_UP, 300);
    quickSetNavBar->bVisible = true;
    currentState = STATE_SET_QUICK;
}

void SendDataPage::setAsQuickKey(int keyIndex) {
    if (quickProfileId < 0) {
        auto list = dataStore.ListHomeProfiles();
        if (!list.empty()) quickProfileId = list[0].id;
    }
    HomeProfile hp;
    if (!dataStore.LoadHomeProfile(quickProfileId, hp)) {
        quickSetBox->hide();
        quickSetNavBar->bVisible=false;
        detailNavBar->bVisible=true;
        currentState = STATE_DETAIL;
        return;
    }
    if (!quickKeyValidForLayout(hp.layout, keyIndex)) {
        // 保留选择界面，提示无效键
        quickSetBox->setTitle("提示");
        quickSetBox->setMessage("该主页无此键");
        quickSetBox->show(MSGBOX_ANIME_BOUNCE_IN, 250);
        quickSetBox->setAutoClose(900);
        return;
    }
    // SINGLE 布局仅键1有效，已通过校验
    hp.slotMap[keyIndex-1] = (int16_t)selectedDataIndex;
    dataStore.SaveHomeProfile(hp);
    quickSetBox->hide();
    quickSetNavBar->bVisible = false;
    detailNavBar->bVisible = true;
    currentState = STATE_DETAIL;
}

void SendDataPage::onButtonBack(void* context) {
    if (rawSending) {
        radioHelper.CancelRawTransmit();
        detailNavBar->setMiddleButtonText("停止中...");
        return;
    }
    switch (currentState) {
        case STATE_SELECT:
            if (bAnimating) return;
            bAnimating = true;
            dataListMenu->getNavBar()->showLeftBlink(1, 80, 80, [this]() {
                uiEngine.navigateBack();
                bAnimating = false;
            });
            break;
        case STATE_DETAIL:
            if (bAnimating) return;
            bAnimating = true;
            detailNavBar->showLeftBlink(1, 80, 80, [this]() {
                nameLabel->bVisible = false;
                detailNavBar->setMiddleButtonText("");
                hideDetailGrid();
                detailNavBar->bVisible = false;
                dataListMenu->bVisible = true;
                currentState = STATE_SELECT;
                bAnimating = false;
            });
            break;
        case STATE_SENDING:
            messageBox->hide();
            messageBox->cancelAutoClose();
            dataListMenu->bVisible = true;
            currentState = STATE_SELECT;
            break;
        case STATE_SET_QUICK_PROFILE:
            if (bAnimating) return;
            bAnimating = true;
            quickProfileMenu->getNavBar()->showLeftBlink(1,80,80,[this](){
                quickProfileMenu->bVisible=false;
                detailNavBar->bVisible=true;
                currentState = STATE_DETAIL;
                bAnimating=false;
            });
            break;
        case STATE_SET_QUICK:
            if (bAnimating) return;
            bAnimating = true;
            quickSetNavBar->showLeftBlink(1, 80, 80, [this]() {
                quickSetBox->hide();
                quickSetNavBar->bVisible = false;
                // 回到主页选择
                enterQuickProfilePick();
                bAnimating = false;
            });
            break;
    }
}

void SendDataPage::onButtonEnter(void* context) {
    if (rawSending) {
        radioHelper.CancelRawTransmit();
        detailNavBar->setMiddleButtonText("停止中...");
        return;
    }
    switch (currentState) {
        case STATE_SELECT: {
            if (bAnimating) return;
            bAnimating = true;
            int menuSel = dataListMenu->menuSel;
            int selectedSlot = (menuSel < slotListCount) ? slotList[menuSel] : -1;
            dataListMenu->getNavBar()->showRightBlink(1, 80, 80, [this, selectedSlot]() {
                showDataDetail(selectedSlot);
                bAnimating = false;
            });
            break;
        }
        case STATE_DETAIL: {
            if (bAnimating) return;
            bAnimating = true;
            bool isRF = (currentData.kind == SIG_RF);
            if (isRF) detailNavBar->setMiddleButtonText("发送中...");
            else nameLabel->label = "发送中...";
            detailNavBar->showRightBlink(1, 80, 80, [this, isRF]() {
                if (isRF && currentData.rf.rawFlag) {
                    rawSending = radioHelper.StartRawTransmit(currentData.rf.rawSlot);
                    if (rawSending) {
                        detailNavBar->setLeftButtonText("停止");
                        detailNavBar->setRightButtonText("停止");
                    } else {
                        detailNavBar->setMiddleButtonText("发送失败");
                        bAnimating = false;
                    }
                    return;
                }
                const bool sent = sendCurrentSignal();
                if (isRF) detailNavBar->setMiddleButtonText(sent ? "已发送" : "发送失败");
                else nameLabel->label = "发送成功";
                bAnimating = false;
            });
            break;
        }
        case STATE_SENDING:
            messageBox->hide();
            messageBox->cancelAutoClose();
            dataListMenu->bVisible = true;
            currentState = STATE_SELECT;
            break;
        case STATE_SET_QUICK_PROFILE: {
            auto list = dataStore.ListHomeProfiles();
            int sel = quickProfileMenu->menuSel;
            if (sel >= 0 && sel < (int)list.size()) quickProfileId = list[sel].id;
            quickProfileMenu->getNavBar()->showRightBlink(1,80,80,[this](){ enterQuickKeyPick(); });
            break;
        }
        case STATE_SET_QUICK:
            setAsQuickKey(1);
            break;
    }
}

void SendDataPage::onButtonMenu(void* context) {
    switch (currentState) {
        case STATE_SELECT:
            dataListMenu->moveDown();
            break;
        case STATE_DETAIL:
            if (bAnimating) return;
            bAnimating = true;
            detailNavBar->showMiddleBlink(1, 80, 80, [this]() {
                enterQuickProfilePick();
                bAnimating = false;
            });
            break;
        case STATE_SET_QUICK_PROFILE:
            quickProfileMenu->moveDown();
            break;
    }
}

void SendDataPage::onButton1(void* context) {
    if (currentState == STATE_SELECT) dataListMenu->moveUp();
    else if (currentState == STATE_DETAIL && currentData.kind == SIG_IR) cycleIRFrame(-1);
    else if (currentState == STATE_SET_QUICK_PROFILE) quickProfileMenu->moveUp();
    else if (currentState == STATE_SET_QUICK) setAsQuickKey(1);
}
void SendDataPage::onButton2(void* context) {
    if (currentState == STATE_SELECT) dataListMenu->moveUp();
    else if (currentState == STATE_SET_QUICK_PROFILE) quickProfileMenu->moveUp();
    else if (currentState == STATE_SET_QUICK) setAsQuickKey(2);
}
void SendDataPage::onButton3(void* context) { if (currentState == STATE_SET_QUICK) setAsQuickKey(3); }
void SendDataPage::onButton4(void* context) { if (currentState == STATE_SET_QUICK) setAsQuickKey(4); }
void SendDataPage::onButton5(void* context) { onButtonEnter(context); }
void SendDataPage::onButton6(void* context) { if (currentState == STATE_SET_QUICK) setAsQuickKey(6); }
void SendDataPage::onButton7(void* context) { if (currentState == STATE_SET_QUICK) setAsQuickKey(7); }
void SendDataPage::onButton8(void* context) {
    if (currentState == STATE_SELECT) dataListMenu->moveDown();
    else if (currentState == STATE_DETAIL && currentData.kind == SIG_IR) cycleIRFrame(1);
    else if (currentState == STATE_SET_QUICK_PROFILE) quickProfileMenu->moveDown();
    else if (currentState == STATE_SET_QUICK) setAsQuickKey(8);
}
void SendDataPage::onButton9(void* context) { if (currentState == STATE_SET_QUICK) setAsQuickKey(9); }
