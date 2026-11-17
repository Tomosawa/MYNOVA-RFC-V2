/* 
* Copyright (c) 2026 Tomosawa 
*/
#ifndef BootAnimPage_h
#define BootAnimPage_h

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIMenu.h"
#include "../../GUI/GUIRender.h"

class BootAnimPage : public UIPage {
public:
    BootAnimPage();
    ~BootAnimPage();
    void showPage() override;
    void render(U8G2* u8g2) override;
    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;
private:
    UIMenu* menu;
    void refreshMenu();
    void applySelected();
};

#endif
