/*
  RawConfigPage.cpp — RAW 录制参数配置页
*/

#include "RawConfigPage.h"
#include "../../GUI/UIEngine.h"
#include "../../GUI/GUIRender.h"
#include "../../Drivers/RadioHelper.h"
#include "../../Lib/RawCaptureSession.h"
#include "RawRecordPage.h"

extern UIEngine uiEngine;

// RSSI 门限候选（0=关闭；其余 dBm，对齐 Flipper Read RAW 门限档）
static const int16_t RAW_THRESHOLDS[] = {
    RAW_THRESHOLD_OFF, -105, -100, -95, -90, -85, -80, -75, -70, -65, -60, -55, -50, -45, -40
};
static const uint8_t RAW_THRESHOLD_COUNT = sizeof(RAW_THRESHOLDS) / sizeof(RAW_THRESHOLDS[0]);

RawConfigPage::RawConfigPage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT) {
    navBar = new UINavBar(0, 52, SCREEN_WIDTH, 12);
    navBar->setMargin(0, 0);
    navBar->bVisible = true;
    navBar->setLeftButtonText("返回");
    navBar->setRightButtonText("开始");
    addWidget(navBar);
}

RawConfigPage::~RawConfigPage() {}

String RawConfigPage::freqText() const {
    return String(RF_FREQ_TABLE_ALL[freqIdx], 2) + "MHz";
}

String RawConfigPage::presetText() const {
    return String(RfPreset(presetIdx).displayName);
}

String RawConfigPage::thresholdText() const {
    int16_t v = RAW_THRESHOLDS[thresholdIdx];
    if (v == RAW_THRESHOLD_OFF) return String("关闭");
    return String(v) + "dBm";
}

void RawConfigPage::changeValue(int8_t delta) {
    if (selRow == 0) {
        int16_t v = (int16_t)freqIdx + delta;
        if (v < 0) v = RF_FREQ_COUNT_ALL - 1;
        if (v >= RF_FREQ_COUNT_ALL) v = 0;
        freqIdx = (uint8_t)v;
    } else if (selRow == 1) {
        int16_t v = (int16_t)presetIdx + delta;
        if (v < 0) v = RF_PRESET_COUNT - 1;
        if (v >= RF_PRESET_COUNT) v = 0;
        presetIdx = (uint8_t)v;
    } else {
        int16_t v = (int16_t)thresholdIdx + delta;
        if (v < 0) v = RAW_THRESHOLD_COUNT - 1;
        if (v >= RAW_THRESHOLD_COUNT) v = 0;
        thresholdIdx = (uint8_t)v;
    }
}

void RawConfigPage::startRecord() {
    RawCaptureConfig cfg;
    cfg.frequencyHz = (uint32_t)(RF_FREQ_TABLE_ALL[freqIdx] * 1000000.0f);
    cfg.presetId = presetIdx;
    cfg.rssiThreshold = RAW_THRESHOLDS[thresholdIdx];
    navBar->showRightBlink(1, 80, 80, [this, cfg]() {
        uiEngine.navigateTo(new RawRecordPage(cfg));
    });
}

void RawConfigPage::render(U8G2* u8g2) {
    // 自绘三行配置项（中文必须 drawUTF8，drawStr 只支持单字节字体）
    u8g2->setFont(u8g2_font_wqy12_t_gb2312);
    u8g2->setDrawColor(1);

    u8g2->drawUTF8(0, 11, "RAW 录制设置");
    u8g2->drawHLine(0, 14, SCREEN_WIDTH);

    const char* names[ROW_COUNT] = {"频率", "调制", "门限"};
    String values[ROW_COUNT] = {freqText(), presetText(), thresholdText()};

    // 标题占 y0~11，三行从 y25 起（基线），行距 12：
    // 末行基线 49、反色框底 51，不压 y=52 的导航栏
    for (uint8_t i = 0; i < ROW_COUNT; i++) {
        int y = 25 + i * 12;
        // 标签始终正常显示；仅右侧的参数值反色。
        u8g2->setDrawColor(1);
        u8g2->drawUTF8(2, y, names[i]);
        if (i == selRow) {
            // 选中行：反色底
            u8g2->setDrawColor(1);
            u8g2->drawRBox(43, y - 10, SCREEN_WIDTH - 44, 12, 2);
            u8g2->setDrawColor(0);
        } else {
            u8g2->setDrawColor(1);
        }
        int valueWidth = u8g2->getUTF8Width(values[i].c_str());
        u8g2->drawUTF8(43 + (SCREEN_WIDTH - 44 - valueWidth) / 2, y, values[i].c_str());
    }
    u8g2->setDrawColor(1);

    UIPage::render(u8g2);
}

void RawConfigPage::onButtonBack(void* context) {
    navBar->showLeftBlink(1, 80, 80, [this]() {
        uiEngine.navigateBack();
    });
}

void RawConfigPage::onButtonEnter(void* context) { startRecord(); }
void RawConfigPage::onButton5(void* context) { startRecord(); }

void RawConfigPage::onButton2(void* context) {
    selRow = (selRow + ROW_COUNT - 1) % ROW_COUNT;
}

void RawConfigPage::onButton8(void* context) {
    selRow = (selRow + 1) % ROW_COUNT;
}

void RawConfigPage::onButton4(void* context) { changeValue(-1); }
void RawConfigPage::onButton6(void* context) { changeValue(1); }
