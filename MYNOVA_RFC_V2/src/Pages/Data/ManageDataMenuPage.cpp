/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#include "ManageDataMenuPage.h"
#include "../../GUI/UIEngine.h"
#include "../../GUI/GUIRender.h"
#include "ManageDataPage.h"
#include "FileManagerPage.h"

extern UIEngine uiEngine;

ManageDataMenuPage::ManageDataMenuPage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT), menu(nullptr) {
    initLayout();
}

ManageDataMenuPage::~ManageDataMenuPage() {}

void ManageDataMenuPage::initLayout() {
    menu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    menu->titleText = "管理数据";
    menu->bShowBtnTips = true;
    menu->bShowBorder = false;
    menu->bShowTitle = false;
    menu->bVisible = true;
    menu->getNavBar()->setLeftButtonText("返回");
    menu->getNavBar()->setRightButtonText("选择");
    menu->addMenuItem("1.射频数据");
    menu->addMenuItem("2.红外数据");
    menu->addMenuItem("3.TF卡数据");
    addWidget(menu);
}

void ManageDataMenuPage::onButtonBack(void* context) {
    menu->getNavBar()->showLeftBlink(1, 80, 80, [this]() { uiEngine.navigateBack(); });
}
void ManageDataMenuPage::onButtonMenu(void* context) { menu->moveDown(); }
void ManageDataMenuPage::onButtonEnter(void* context) {
    int sel = menu->menuSel;
    menu->getNavBar()->showRightBlink(1, 80, 80, [this, sel]() {
        switch (sel) {
            case 0: uiEngine.navigateTo(new ManageDataPage(SIG_RF)); break;
            case 1: uiEngine.navigateTo(new ManageDataPage(SIG_IR)); break;
            case 2: uiEngine.navigateTo(new FileManagerPage()); break;
            default: break;
        }
    });
}
void ManageDataMenuPage::onButton2(void* context) { menu->moveUp(); }
void ManageDataMenuPage::onButton5(void* context) { onButtonEnter(context); }
void ManageDataMenuPage::onButton8(void* context) { menu->moveDown(); }
