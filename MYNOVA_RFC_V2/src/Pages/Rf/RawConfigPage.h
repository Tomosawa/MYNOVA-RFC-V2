/*
  RawConfigPage.h — RAW 录制参数配置页（频率 / 调制 / RSSI 门限）

  按键：2/8 上·下换项，4/6 左·右改值，5/确定 开始录制，返回键退出
*/

#ifndef RAW_CONFIG_PAGE_H
#define RAW_CONFIG_PAGE_H

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../Lib/RFSubGhzPresets.h"

class RawConfigPage : public UIPage {
public:
    RawConfigPage();
    ~RawConfigPage();

    void render(U8G2* u8g2) override;

    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;   // 上一项
    void onButton8(void* context = nullptr) override;   // 下一项
    void onButton4(void* context = nullptr) override;   // 值 -1
    void onButton6(void* context = nullptr) override;   // 值 +1
    void onButton5(void* context = nullptr) override;   // 开始录制

private:
    UINavBar* navBar = nullptr;
    uint8_t  selRow = 0;       // 0频率 1调制 2门限
    uint8_t  freqIdx = 9;     // RF_FREQ_TABLE_ALL 下标（默认 433.92）
    uint8_t  presetIdx = RF_PRESET_AM650;
    // 默认 -85dBm（档位表下标 5），弱信号可下调到 -90..-105dBm。
    // 关闭门限会连续记录异步数据噪声，消耗文件段数；默认带门限。
    uint8_t  thresholdIdx = 5;

    static const uint8_t ROW_COUNT = 3;
    void changeValue(int8_t delta);
    void startRecord();
    String freqText() const;
    String presetText() const;
    String thresholdText() const;
};

#endif // RAW_CONFIG_PAGE_H
