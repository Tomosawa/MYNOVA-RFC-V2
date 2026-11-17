/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef __RFMENUPAGE_H__
#define __RFMENUPAGE_H__

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIMenu.h"

/**
 * RFMenuPage - 射频遥控子菜单页面
 * 入口：射频接收（先选频段） / 射频发送（从存储的射频信号中选择发射）
 * 与 IRMenuPage 结构对称
 */
class RFMenuPage : public UIPage {
public:
    RFMenuPage();
    ~RFMenuPage();

    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;

private:
    void initLayout();
    UIMenu* rfMenu;
};

#endif
