/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// RecordSourcePage.cpp
// 按键: 2=上移 8/MENU=下移 5/ENTER=选择进入录音 BACK=返回
#include "RecordSourcePage.h"
#include "RecordPage.h"
#include "../../GUI/UIEngine.h"
#include "../../GUI/GUIRender.h"

extern UIEngine uiEngine;

RecordSourcePage::RecordSourcePage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT),
    srcMenu(nullptr) {
    initLayout();
}

RecordSourcePage::~RecordSourcePage() {
}

void RecordSourcePage::initLayout() {
    // 初始化输入源选择菜单(菜单项序号与RecSource枚举值一一对应)
    srcMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    srcMenu->titleText = "选择录音输入";
    srcMenu->bShowBtnTips = true;
    srcMenu->bShowBorder = false;
    srcMenu->bShowTitle = false;
    srcMenu->bVisible = true;

    // 通过导航栏设置按钮文字
    srcMenu->getNavBar()->setLeftButtonText("返回");
    srcMenu->getNavBar()->setRightButtonText("选择");

    srcMenu->addMenuItem("1. 内置麦克风");
    srcMenu->addMenuItem("2. 外置耳机麦克风");
    srcMenu->addMenuItem("3. 双麦克风立体声");
    addWidget(srcMenu);
}

void RecordSourcePage::onButtonBack(void* context) {
    // 显示左键闪烁动画，动画完成后返回上一页
    srcMenu->getNavBar()->showLeftBlink(1, 80, 80, [this]() {
        uiEngine.navigateBack();
    });
}

void RecordSourcePage::onButtonMenu(void* context) {
    srcMenu->moveDown();
}

void RecordSourcePage::onButtonEnter(void* context) {
    // 显示右键闪烁动画，动画完成后带所选源进入录音页面
    int selectedItem = srcMenu->menuSel;
    srcMenu->getNavBar()->showRightBlink(1, 80, 80, [this, selectedItem]() {
        // 菜单项索引即RecSource: 0=内置麦克风 1=外置耳机麦克风 2=双麦克风
        uiEngine.navigateTo(new RecordPage(selectedItem));
    });
}

void RecordSourcePage::onButton2(void* context) {
    srcMenu->moveUp();
}

void RecordSourcePage::onButton5(void* context) {
    onButtonEnter(context);
}

void RecordSourcePage::onButton8(void* context) {
    srcMenu->moveDown();
}
