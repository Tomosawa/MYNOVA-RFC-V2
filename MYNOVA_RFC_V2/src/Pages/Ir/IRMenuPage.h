/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef __IRMENUPAGE_H__
#define __IRMENUPAGE_H__

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIMenu.h"

/**
 * IRMenuPage - 红外遥控子菜单页面
 * 入口：红外学习（接收并保存） / 红外发送（从存储的红外信号中选择发射）
 */
class IRMenuPage : public UIPage {
public:
    IRMenuPage();
    ~IRMenuPage();

    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;

private:
    void initLayout();
    UIMenu* irMenu;   // 红外功能菜单
};

#endif
