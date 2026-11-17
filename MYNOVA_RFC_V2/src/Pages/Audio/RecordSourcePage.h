/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// RecordSourcePage.h
// 录音输入源选择页面: 进入录音界面前选择麦克风(同接收模式先选频段的流程)
//   1.内置麦克风   → 单声道WAV(88.2KB/s, 省空间)
//   2.外置耳机麦克风 → 单声道WAV(耳机未插时将录到静音)
//   3.双麦克风     → 立体声WAV(176.4KB/s, L=内置/R=外置)
#ifndef RecordSourcePage_h
#define RecordSourcePage_h

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIMenu.h"
#include "../../Drivers/AudioPlayer.h"

class RecordSourcePage : public UIPage {
public:
    RecordSourcePage();
    ~RecordSourcePage() override;

    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;

private:
    void initLayout();
    UIMenu* srcMenu;   // 输入源选择菜单
};

#endif
