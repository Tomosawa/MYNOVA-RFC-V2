/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#include "IRMenuPage.h"
#include "IRLearnPage.h"
#include "../Rf/SendDataPage.h"
#include "../../GUI/UIEngine.h"
#include "../../GUI/GUIRender.h"

extern UIEngine uiEngine;

IRMenuPage::IRMenuPage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT),
    irMenu(nullptr) {
    initLayout();
}

IRMenuPage::~IRMenuPage() {
}

void IRMenuPage::initLayout() {
    // 初始化红外功能菜单
    irMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    irMenu->titleText = "红外遥控";
    irMenu->bShowBtnTips = true;
    irMenu->bShowBorder = false;
    irMenu->bShowTitle = false;
    irMenu->bVisible = true;

    // 通过导航栏设置按钮文字
    irMenu->getNavBar()->setLeftButtonText("返回");
    irMenu->getNavBar()->setRightButtonText("选择");

    irMenu->addMenuItem("1. 红外学习");
    irMenu->addMenuItem("2. 红外发送");
    addWidget(irMenu);
}

void IRMenuPage::onButtonBack(void* context) {
    // 显示左键闪烁动画，动画完成后返回上一页
    irMenu->getNavBar()->showLeftBlink(1, 80, 80, [this]() {
        uiEngine.navigateBack();
    });
}

void IRMenuPage::onButtonMenu(void* context) {
    irMenu->moveDown();
}

void IRMenuPage::onButtonEnter(void* context) {
    // 显示右键闪烁动画，动画完成后跳转对应页面
    int selectedItem = irMenu->menuSel;
    irMenu->getNavBar()->showRightBlink(1, 80, 80, [this, selectedItem]() {
        switch (selectedItem) {
            case 0:
                // 红外学习（接收并保存）
                uiEngine.navigateTo(new IRLearnPage());
                break;
            case 1:
                // 红外发送（仅列出红外信号的发送列表）
                uiEngine.navigateTo(new SendDataPage(FILTER_IR));
                break;
            default:
                break;
        }
    });
}

void IRMenuPage::onButton2(void* context) {
    irMenu->moveUp();
}

void IRMenuPage::onButton5(void* context) {
    onButtonEnter(context);
}

void IRMenuPage::onButton8(void* context) {
    irMenu->moveDown();
}
