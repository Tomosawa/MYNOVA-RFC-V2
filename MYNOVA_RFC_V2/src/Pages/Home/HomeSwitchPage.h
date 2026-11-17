/* 
* Copyright (c) 2026 Tomosawa 
*/
#ifndef HOMESWITCHPAGE_H
#define HOMESWITCHPAGE_H

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIMenu.h"
#include "../../Services/DataStore.h"

class HomeSwitchPage : public UIPage {
public:
    HomeSwitchPage();
    ~HomeSwitchPage() override;
    void showPage() override;
    void render(U8G2* u8g2) override;
    void onButtonBack(void* context=nullptr) override;
    void onButtonEnter(void* context=nullptr) override;
    void onButtonMenu(void* context=nullptr) override;
    void onButton2(void* context=nullptr) override;
    void onButton5(void* context=nullptr) override;
    void onButton8(void* context=nullptr) override;
private:
    void refreshMenu();
    void activateSelected();
    std::vector<HomeProfile> profiles;
    UIMenu* menu = nullptr;
};

#endif
