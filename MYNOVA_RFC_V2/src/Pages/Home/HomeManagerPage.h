/* 
* Copyright (c) 2026 Tomosawa 
*/
#ifndef HOMEMANAGERPAGE_H
#define HOMEMANAGERPAGE_H

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIMenu.h"
#include "../../GUI/Widget/UILabel.h"
#include "../../GUI/Widget/UIMessageBox.h"
#include "../../GUI/Widget/UIInput.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../Services/DataStore.h"

class HomeManagerPage : public UIPage {
public:
    HomeManagerPage();
    ~HomeManagerPage() override;
    void showPage() override;
    void render(U8G2* u8g2) override;
    void onButtonBack(void* context=nullptr) override;
    void onButtonEnter(void* context=nullptr) override;
    void onButtonMenu(void* context=nullptr) override;
    void onButton1(void* context=nullptr) override;
    void onButton2(void* context=nullptr) override;
    void onButton3(void* context=nullptr) override;
    void onButton4(void* context=nullptr) override;
    void onButton5(void* context=nullptr) override;
    void onButton6(void* context=nullptr) override;
    void onButton7(void* context=nullptr) override;
    void onButton8(void* context=nullptr) override;
    void onButton9(void* context=nullptr) override;
private:
    enum State { STATE_LIST, STATE_ACTION, STATE_CREATE, STATE_RENAME, STATE_DELETE_CONFIRM, STATE_TIP };
    enum CreateStage { CREATE_PICK_LAYOUT, CREATE_INPUT_NAME };
    void refreshList();
    void enterAction();
    void doSetActive();
    void doEditKeys();
    void enterRename();
    void doRename();
    void enterDeleteConfirm();
    void doDelete();
    void enterCreateLayout();
    void enterCreateName();
    void doCreate();
    void showTip(const String& msg);

    State state = STATE_LIST;
    CreateStage createStage = CREATE_PICK_LAYOUT;
    uint8_t createLayout = HOME_LAYOUT_GRID9;
    std::vector<HomeProfile> profiles;
    UIMenu* listMenu = nullptr;
    UIMenu* actionMenu = nullptr;
    UIMenu* layoutMenu = nullptr;
    UIInput* nameInput = nullptr;
    UINavBar* inputNavBar = nullptr;
    UINavBar* confirmNavBar = nullptr;
    UIMessageBox* confirmBox = nullptr;
    UIMessageBox* tipBox = nullptr;
    int renameProfileId = -1;
};

#endif
