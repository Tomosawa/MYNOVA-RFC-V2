/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

// ReceivePage.cpp
#include "ReceivePage.h"
#include "../../GUI/UIEngine.h"
#include "../../GUI/GUIRender.h"
#include "../Data/SaveDataPage.h"

extern UIEngine uiEngine;
extern RadioHelper radioHelper;
extern GUIRender guiRender;

ReceivePage::ReceivePage(RFBand rfBand) : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT),
    freqLabel(nullptr),
    dataLabel(nullptr),
    bitLengthLabel(nullptr),
    pulseLengthLabel(nullptr),
    protocolLabel(nullptr),
    navBar(nullptr),
    band(rfBand),
    fixedFreq(false),
    fixedFreqMHz(0.0f),
    currentState(STATE_RECEIVING),
    lastFreqShowTime(0) {
    initLayout();
}

ReceivePage::ReceivePage(float freqMHz) : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT),
    band(RFGetBandByFrequency(freqMHz)),
    fixedFreq(true),
    fixedFreqMHz(freqMHz),
    currentState(STATE_RECEIVING),
    lastFreqShowTime(0) {
    initLayout();
}

ReceivePage::~ReceivePage() {
    // 停止接收
    stopReceiving();
}

void ReceivePage::initLayout() {
    // 无状态行：扫描/成功提示放底部导航栏中间
    // 第一行：频率 + 位长（扫描过程中频率实时显示当前频点）
    freqLabel = new UILabel();
    freqLabel->x = 0;
    freqLabel->y = 0;
    freqLabel->width = 72;
    freqLabel->height = 12;
    freqLabel->label = "频率: ---";
    freqLabel->textAlign = LEFT;
    freqLabel->verticalAlign = MIDDLE;
    freqLabel->bVisible = true;
    addWidget(freqLabel);

    // 第三行：协议独占
    protocolLabel = new UILabel();
    protocolLabel->x = 0;
    protocolLabel->y = 26;
    protocolLabel->width = SCREEN_WIDTH;
    protocolLabel->height = 12;
    protocolLabel->label = "协议: --";
    protocolLabel->textFont = u8g2_font_wqy12_t_gb2312;
    protocolLabel->textAlign = LEFT;
    protocolLabel->verticalAlign = MIDDLE;
    protocolLabel->bVisible = true;
    addWidget(protocolLabel);
    
    // 第一行右侧：位长
    bitLengthLabel = new UILabel();
    bitLengthLabel->x = 72;
    bitLengthLabel->y = 0;
    bitLengthLabel->width = SCREEN_WIDTH - 72;
    bitLengthLabel->height = 12;
    bitLengthLabel->label = "位长: --";
    bitLengthLabel->textFont = u8g2_font_wqy12_t_gb2312;
    bitLengthLabel->textAlign = LEFT;
    bitLengthLabel->verticalAlign = MIDDLE;
    bitLengthLabel->bVisible = true;
    addWidget(bitLengthLabel);
    
    // 第二行：脉宽独占
    pulseLengthLabel = new UILabel();
    pulseLengthLabel->x = 0;
    pulseLengthLabel->y = 13;
    pulseLengthLabel->width = SCREEN_WIDTH;
    pulseLengthLabel->height = 12;
    pulseLengthLabel->label = "脉宽: --";
    pulseLengthLabel->textFont = u8g2_font_wqy12_t_gb2312;
    pulseLengthLabel->textAlign = LEFT;
    pulseLengthLabel->verticalAlign = MIDDLE;
    pulseLengthLabel->bVisible = true;
    addWidget(pulseLengthLabel);

    // 第四行：数据独占
    dataLabel = new UILabel();
    dataLabel->x = 0;
    dataLabel->y = 39;
    dataLabel->width = SCREEN_WIDTH;
    dataLabel->height = 12;
    dataLabel->label = "数据: 等待信号...";
    dataLabel->textFont = u8g2_font_wqy12_t_gb2312;
    dataLabel->textAlign = LEFT;
    dataLabel->verticalAlign = MIDDLE;
    dataLabel->bVisible = true;
    addWidget(dataLabel);

    // 创建导航栏
    navBar = new UINavBar(0, 52, SCREEN_WIDTH, 12);
    navBar->setMargin(0, 0);
    navBar->bVisible = true;
    navBar->setLeftButtonText("返回");
    navBar->setMiddleButtonText(fixedFreq ? "监听中" : "扫描中");
    addWidget(navBar);

    startReceiving();
}

void ReceivePage::startReceiving() {
    currentState = STATE_RECEIVING;
    
    // 更新显示状态（提示放底部导航栏中间）
    navBar->setMiddleButtonText(fixedFreq ? "监听中" : "扫描中");
    freqLabel->label = "频率: ---";
    dataLabel->label = "数据: 等待信号...";
    bitLengthLabel->label = "位长: --";
    protocolLabel->label = "协议: --";
    pulseLengthLabel->label = "脉宽: --";
    navBar->setRightButtonText("");
    
    // 启动CC1101接收：单频点守候或频段扫描（天线开关内部自动切换）
    if (fixedFreq) radioHelper.EnableReceiveFixed(fixedFreqMHz);
    else radioHelper.EnableReceive(band);
    
    lastFreqShowTime = 0;
}

void ReceivePage::stopReceiving() {
    // 只有在接收模式下才禁用接收
    if (currentState == STATE_RECEIVING) {
        radioHelper.DisableReceive();
    }
}

String ReceivePage::formatHexData(const RFSignalData& data) {
    // 最多显示前4字节的HEX（完整数据在保存后可查看）
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

void ReceivePage::updateReceiveStatus() {
    if (currentState != STATE_RECEIVING) return;

    // 接收任务完成后，下次绘制即可显示结果。频率提示仍按 200ms 刷新。
    if (!radioHelper.hasNewSignal) {
        if (!radioHelper.IsReceiving()) {
            navBar->setMiddleButtonText("接收失败");
            navBar->setRightButtonText("重试");
            dataLabel->label = "射频启动失败";
            return;
        }
        const unsigned long now = millis();
        if (now - lastFreqShowTime > 200) {
            lastFreqShowTime = now;
            const float freq = radioHelper.GetCurrentScanFrequency();
            if (freq > 0.0f) freqLabel->label = "频率:" + String(freq, 2) + "M";
        }
        return;
    }

    const RFSignalData& rc = radioHelper.rcData;
    currentState = STATE_RECEIVED;
    navBar->setMiddleButtonText("接收成功");
    freqLabel->label = "频率:" + String(rc.frequency, 2) + "M";
    bitLengthLabel->label = "位长:" + String(rc.bitCount) + "bit";
    pulseLengthLabel->label = "脉宽:" + String(rc.shortPulse) + "/" + String(rc.longPulse);
    protocolLabel->label = "协议:" + String(rc.protocolName[0] ? rc.protocolName : "RAW");

    String dataStr = formatHexData(rc);
    if (rc.bitCount > 40) dataStr += "...";
    dataLabel->label = "数据: " + dataStr;
    navBar->setRightButtonText("保存");
    radioHelper.ClearNewSignal();  // rcData 保留给保存页面
}

void ReceivePage::showPage() {
    
}

void ReceivePage::render(U8G2* u8g2) {
    // 先更新接收状态
    updateReceiveStatus();
    // 然后调用父类的 render 方法来渲染所有组件
    UIPage::render(u8g2);
}

void ReceivePage::onButtonBack(void* context) {
    if (currentState == STATE_RECEIVING)
    {
        // 显示左键闪烁动画，动画完成后返回上一页
        navBar->showLeftBlink(1, 80, 80, [this]() {
            stopReceiving();
            radioHelper.ClearNewSignal();
            // 返回上一页
            uiEngine.navigateBack();
        });
    }
    else if(currentState == STATE_RECEIVED)
    {
        // 显示左键闪烁动画，动画完成后返回上一页
        navBar->showLeftBlink(1, 80, 80, [this]() {
            radioHelper.ClearNewSignal();
            // 返回上一页
            uiEngine.navigateBack();
        });
    }
}

void ReceivePage::onButtonEnter(void* context) {
    if (currentState == STATE_RECEIVING && !radioHelper.IsReceiving() && !radioHelper.hasNewSignal) {
        startReceiving();
        return;
    }
    if (currentState == STATE_RECEIVED) {
        // 显示右键闪烁动画，动画完成后跳转到保存页面
        navBar->showRightBlink(1, 80, 80, [this]() {
            // 跳转到保存页面（以射频信号类型保存）
            SignalDraft draft;
            draft.kind = SIG_RF;
            draft.rf = radioHelper.rcData;
            SaveDataPage* saveDataPage = new SaveDataPage(draft);
            uiEngine.navigateTo(saveDataPage);
        });
    }
}
