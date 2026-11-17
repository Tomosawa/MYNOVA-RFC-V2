/* RAW录制页：参数/时间、RSSI图、状态/计数、实体按键，四个独立区域。 */
#include "RawRecordPage.h"
#include "../../Services/RawSignalFiles.h"
#include "../../GUI/UIEngine.h"
#include "../../GUI/GUIRender.h"
#include "../../Drivers/RadioHelper.h"
#include "../Data/SaveDataPage.h"

extern UIEngine uiEngine;
extern RadioHelper radioHelper;

static const int GRAPH_Y0 = 12;
static const int GRAPH_Y1 = 38;

RawRecordPage::RawRecordPage(const RawCaptureConfig& captureCfg)
    : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT), cfg(captureCfg) {
    navBar = new UINavBar(0, 52, SCREEN_WIDTH, 12);
    navBar->setMargin(3, 3);
    navBar->bVisible = true;
    addWidget(navBar);
    updateButtons();
}

RawRecordPage::~RawRecordPage() {
    if (state == RAW_PAGE_SENDING) radioHelper.CancelRawTransmit();
    session.stop();
}

void RawRecordPage::showPage() {
    if (firstShow) {
        firstShow = false;
        beginRecord();  // 配置页的“开始”直接开始录制
    }
}

void RawRecordPage::update() {
    if (state == RAW_PAGE_REC && !session.snapshot().recording) stopRecord();
    if (state == RAW_PAGE_SENDING) {
        RawTransmitState result = radioHelper.GetRawTransmitState();
        if (result != RawTransmitState::Sending) {
            state = RAW_PAGE_IDLE;
            notice = result == RawTransmitState::Sent ? "已发送" :
                     result == RawTransmitState::Cancelled ? "已停止" : "发送失败";
            noticeUntil = millis() + 2000;
            updateButtons();
        }
    }
}

String RawRecordPage::topInfo() const {
    return String((float)cfg.frequencyHz / 1000000.0f, 2) + "M " +
           RfPreset(cfg.presetId).displayName;
}

void RawRecordPage::updateButtons() {
    bool hasData = session.hasData();
    navBar->setLeftButtonText(state == RAW_PAGE_REC || state == RAW_PAGE_SENDING ? "停止" : "返回");
    navBar->setMiddleButtonText(state == RAW_PAGE_IDLE && hasData ? "保存" : "");
    navBar->setRightButtonText(state == RAW_PAGE_REC || state == RAW_PAGE_SENDING ? "停止" :
                              state == RAW_PAGE_IDLE ? (hasData ? "发送" : "重录") : "录制");
}

void RawRecordPage::beginRecord() {
    notice = "";
    if (!session.start(radioHelper, cfg)) {
        notice = session.errorText();
        noticeUntil = 0; // 启动错误保持显示，Enter可重试
        updateButtons();
        return;
    }
    state = RAW_PAGE_REC;
    updateButtons();
}

void RawRecordPage::stopRecord() {
    session.stop();
    state = RAW_PAGE_IDLE;
    updateButtons();
}

void RawRecordPage::sendRecord() {
    noticeUntil = millis() + 2000;
    if (!session.hasData()) {
        notice = "无数据";
        return;
    }
    if (!radioHelper.StartRawTransmit()) {
        notice = "发送失败";
        return;
    }
    state = RAW_PAGE_SENDING;
    notice = "发送中";
    noticeUntil = 0;
    updateButtons();
}

void RawRecordPage::saveRecord() {
    if (!session.hasData()) return;
    if (!RawSignalFiles::ensureCard()) {
        notice = "请插入TF卡";
        noticeUntil = millis() + 3000;
        return;
    }
    SignalDraft draft;
    draft.kind = SIG_RF;
    draft.rawCapture = true;
    draft.rawTempPath = RAW_TEMP_FILE;
    draft.rf.frequency = (float)cfg.frequencyHz / 1000000.0f;
    draft.rf.rawFlag = 1;
    draft.rf.bitCount = 0;
    strncpy(draft.rf.protocolName, "RAW", sizeof(draft.rf.protocolName) - 1);
    strncpy(draft.rf.rawPreset, RfPreset(cfg.presetId).displayName,
            sizeof(draft.rf.rawPreset) - 1);
    draft.rf.rawCount = (uint16_t)((session.sampleCount() > 65535u)
                                   ? 65535u : session.sampleCount());
    // RF菜单 → 配置页 → 录制页 → 保存页：成功后回退 3 层
    uiEngine.navigateTo(new SaveDataPage(draft, -1, 3));
}

void RawRecordPage::eraseRecord() {
    session.erase();
    state = RAW_PAGE_START;
    notice = "";
    updateButtons();
}

void RawRecordPage::drawGraph(U8G2* u8g2, const RawCaptureSnapshot& s) {
    u8g2->drawFrame(0, GRAPH_Y0, SCREEN_WIDTH, GRAPH_Y1 - GRAPH_Y0 + 1);
    uint8_t count = s.wrapped ? RAW_RSSI_HISTORY : s.head;
    uint8_t oldest = s.wrapped ? s.head : 0;
    for (uint8_t i = 0; i < count; i++) {
        // 环形缓冲绕回后仍按时间排列，最新样本始终在右侧。
        int x = SCREEN_WIDTH - 2 -
                (int)((count - 1 - i) * (SCREEN_WIDTH - 3)) / (RAW_RSSI_HISTORY - 1);
        uint8_t h = s.history[(oldest + i) % RAW_RSSI_HISTORY];
        if (h) u8g2->drawVLine(x, GRAPH_Y1 - 1 - h, h);
    }
    if (cfg.rssiThreshold != RAW_THRESHOLD_OFF) {
        int y = GRAPH_Y1 - 1 - RawCaptureSession::rssiToBar(cfg.rssiThreshold);
        for (int x = 2; x < SCREEN_WIDTH - 2; x += 4) u8g2->drawPixel(x, y);
    }
}

void RawRecordPage::render(U8G2* u8g2) {
    RawCaptureSnapshot s = session.snapshot();
    u8g2->setDrawColor(1);
    u8g2->setFontMode(1);
    u8g2->setFont(u8g2_font_5x8_tf);
    u8g2->drawStr(0, 8, topInfo().c_str());
    char elapsed[20];
    snprintf(elapsed, sizeof(elapsed), "%lu.%lus",
             (unsigned long)(s.elapsedMs / 1000), (unsigned long)((s.elapsedMs / 100) % 10));
    u8g2->drawStr(SCREEN_WIDTH - u8g2->getStrWidth(elapsed), 8, elapsed);

    drawGraph(u8g2, s);

    // 所有状态提示只出现在图表下方。REC文字保持稳定。
    const char* status = "未开始";
    if (s.writeError) status = "存储异常";
    else if (s.overrun) status = "丢帧重录";
    else if (s.full) status = "录满重录";
    else if (state == RAW_PAGE_REC) {
        status = !s.gateOpen ? "等信号" :
                 (s.elapsedMs > 1000 && s.edges == 0) ? "无脉冲" : "录制中";
    } else if (state == RAW_PAGE_IDLE) status = s.samples ? "4:清除" : "无数据";
    if (notice.length() && (noticeUntil == 0 || (int32_t)(noticeUntil - millis()) > 0)) {
        status = notice.c_str();
    }
    u8g2->setFont(u8g2_font_wqy12_t_gb2312);
    u8g2->drawUTF8(0, 49, status);
    u8g2->setFont(u8g2_font_5x8_tf);
    char rssi[12], count[16];
    snprintf(rssi, sizeof(rssi), "%ddBm", (int)s.rssi);
    snprintf(count, sizeof(count), "N:%lu", (unsigned long)s.samples);
    u8g2->drawStr(52, 49, rssi);
    u8g2->drawStr(SCREEN_WIDTH - u8g2->getStrWidth(count), 49, count);
    u8g2->setFontMode(0);
    UIPage::render(u8g2);
}

void RawRecordPage::onButtonBack(void* context) {
    if (state == RAW_PAGE_SENDING) {
        radioHelper.CancelRawTransmit();
        notice = "停止中";
        return;
    }
    if (state == RAW_PAGE_REC) {
        stopRecord();
        return;
    }
    session.erase();
    uiEngine.navigateBack();
}

void RawRecordPage::onButtonEnter(void* context) {
    if (state == RAW_PAGE_SENDING) {
        radioHelper.CancelRawTransmit();
        notice = "停止中";
    }
    else if (state == RAW_PAGE_START) beginRecord();
    else if (state == RAW_PAGE_REC) stopRecord();
    else if (session.hasData()) sendRecord();
    else beginRecord();
}

void RawRecordPage::onButtonMenu(void* context) {
    if (state == RAW_PAGE_IDLE) saveRecord();
}

void RawRecordPage::onButton5(void* context) { onButtonEnter(context); }
void RawRecordPage::onButton4(void* context) {
    if (state == RAW_PAGE_IDLE) eraseRecord();
}
void RawRecordPage::onButton6(void* context) {
    if (state == RAW_PAGE_IDLE) saveRecord();
}
