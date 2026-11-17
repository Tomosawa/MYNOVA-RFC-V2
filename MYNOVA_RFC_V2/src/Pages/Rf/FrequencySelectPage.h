/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef __FREQSELECTPAGE_H__
#define __FREQSELECTPAGE_H__

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIMenu.h"
#include "../../Drivers/RadioHelper.h"

/**
 * FrequencySelectPage - 射频接收频段选择页面
 * 进入接收模式前选择扫描频段：
 *   低频段 310~350MHz / 高频段 390~434MHz（由外部天线开关切换通路）
 */
class FrequencySelectPage : public UIPage {
public:
    FrequencySelectPage();
    ~FrequencySelectPage();

    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;

private:
    void initLayout();
    UIMenu* bandMenu;   // 频段选择菜单
};

#endif
