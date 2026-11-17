/* 
* Copyright (c) 2026 Tomosawa 
*/
#include "HomeSwitchPage.h"
#include "../../GUI/UIEngine.h"
#include "../../GUI/GUIRender.h"
#include "../../Services/DataStore.h"

extern UIEngine uiEngine;
extern DataStore dataStore;

static String layoutSuffix(uint8_t l) {
    int n = HomeLayoutToKeyCount(l);
    if (n == 1) return " [单键]";
    return " [" + String(n) + "键]";
}

HomeSwitchPage::HomeSwitchPage() : UIPage(0,0,SCREEN_WIDTH,SCREEN_HEIGHT) {
    menu = new UIMenu(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,5);
    menu->bShowBtnTips=true; menu->bShowBorder=false; menu->bShowTitle=false;
    menu->setShowRadio(true);
    menu->getNavBar()->setLeftButtonText("返回");
    menu->getNavBar()->setRightButtonText("选择");
    addWidget(menu);
    refreshMenu();
}

HomeSwitchPage::~HomeSwitchPage() {}

void HomeSwitchPage::refreshMenu() {
    int keepRadio = menu->bShowRadio;
    menu->clear();
    menu->setShowRadio(keepRadio);
    profiles = dataStore.ListHomeProfiles();
    int active = dataStore.GetActiveHomeProfile();
    for (auto& p: profiles) {
        String s = p.name + layoutSuffix(p.layout);
        menu->addMenuItem(s);
    }
    int activeIdx = -1;
    for (int i = 0; i < (int)profiles.size(); i++) if (profiles[i].id == active) { activeIdx = i; break; }
    menu->setRadioSelected(activeIdx);
}

void HomeSwitchPage::showPage() { UIPage::showPage(); refreshMenu(); }
void HomeSwitchPage::render(U8G2* u8g2) { UIPage::render(u8g2); }

void HomeSwitchPage::activateSelected() {
    int sel = menu->menuSel;
    if (sel < 0 || sel >= (int)profiles.size()) return;
    int pid = profiles[sel].id;
    dataStore.SetActiveHomeProfile(pid);
    menu->setRadioSelected(sel);
    uiEngine.navigateBackSteps(2);
}

void HomeSwitchPage::onButtonBack(void* context) {
    menu->getNavBar()->showLeftBlink(1,80,80,[this](){ uiEngine.navigateBack(); });
}
void HomeSwitchPage::onButtonEnter(void* context) {
    menu->getNavBar()->showRightBlink(1,80,80,[this](){ activateSelected(); });
}
void HomeSwitchPage::onButtonMenu(void* context) { menu->moveDown(); }
void HomeSwitchPage::onButton2(void* context) { menu->moveUp(); }
void HomeSwitchPage::onButton5(void* context) { onButtonEnter(context); }
void HomeSwitchPage::onButton8(void* context) { menu->moveDown(); }
