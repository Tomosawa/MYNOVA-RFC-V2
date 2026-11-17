/* 
* Copyright (c) 2026 Tomosawa 
*/
#include "BootAnimPage.h"
#include "../../GUI/UIEngine.h"
#include "../../Services/SystemSetting.h"
#include "../../Drivers/SplashScreen.h"
#include "../../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "BOOTANIM"

extern UIEngine uiEngine;
extern SystemSetting systemSetting;

// 与 SplashScreen.h 的 BOOT_ANIM_* 一一对应（0=关闭在最下）
static const char* kBootAnimItems[] = {
    "1. 波浪LOGO",
    "2. 雷达扫描",
    "3. 星空流星",
    "4. 打字机",
    "5. 矩阵雨",
    "6. 关闭动画"
};
// style 值与菜单行对应：行0..4 -> style 1..5，行5 -> style 0
static int rowToStyle(int row) { return (row >= 0 && row <= 4) ? (row + 1) : 0; }
static int styleToRow(int style) { return (style >= 1 && style <= 5) ? (style - 1) : 5; }

BootAnimPage::BootAnimPage() : UIPage(0,0,SCREEN_WIDTH,SCREEN_HEIGHT) {
    menu = new UIMenu(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,5);
    menu->bShowBtnTips = true;
    menu->bShowBorder = false;
    menu->bShowTitle = false;
    menu->setShowRadio(true);
    menu->getNavBar()->setLeftButtonText("返回");
    menu->getNavBar()->setRightButtonText("选择");
    addWidget(menu);
    refreshMenu();
}

BootAnimPage::~BootAnimPage() {}

void BootAnimPage::refreshMenu() {
    int keepRadio = menu->bShowRadio;
    menu->clear();
    menu->setShowRadio(keepRadio);
    for (int i = 0; i < 6; i++) menu->addMenuItem(kBootAnimItems[i]);
    int row = styleToRow(systemSetting.getBootAnim());
    LOG_I("BootAnimPage: bootAnim=%d row=%d",
          systemSetting.getBootAnim(), row);
    // 光标同步到当前保存项（保证可见），再打 radio 选中点
    int itemLines = 5 - 1;  // menuLines(5) - 底部按钮提示行
    if (row < itemLines) { menu->menuStart = 0; menu->menuPos = row; }
    else { menu->menuStart = row - itemLines + 1; menu->menuPos = itemLines - 1; }
    menu->menuSelY = menu->menuPos * menu->menuLineHeight;
    menu->menuSel = row;
    menu->setRadioSelected(row);
}

void BootAnimPage::showPage() { UIPage::showPage(); refreshMenu(); }
void BootAnimPage::render(U8G2* u8g2) { UIPage::render(u8g2); }

// radio 只标记已保存的选项, 不跟随光标(光标高亮框负责指示当前行)
void BootAnimPage::applySelected() {
    int sel = menu->menuSel;
    if (sel < 0 || sel > 5) return;
    systemSetting.setBootAnim(rowToStyle(sel), true);
    menu->setRadioSelected(sel);
    uiEngine.navigateBack();
}

void BootAnimPage::onButtonBack(void* context) {
    menu->getNavBar()->showLeftBlink(1,80,80,[this](){ uiEngine.navigateBack(); });
}
void BootAnimPage::onButtonEnter(void* context) {
    menu->getNavBar()->showRightBlink(1,80,80,[this](){ applySelected(); });
}
void BootAnimPage::onButtonMenu(void* context) { menu->moveDown(); }
void BootAnimPage::onButton2(void* context) { menu->moveUp(); }
void BootAnimPage::onButton5(void* context) { onButtonEnter(context); }
void BootAnimPage::onButton8(void* context) { menu->moveDown(); }
