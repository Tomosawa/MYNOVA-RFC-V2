/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// TalkSettingPage.h - 对讲设置页: 设备名(T9)/信道(1-13)/模式默认/关于
// 按键: 2/8=上下移动或改值 4/6=左右切字段 ENTER=保存并返回 BACK=返回
#ifndef TalkSettingPage_h
#define TalkSettingPage_h

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIInput.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../GUI/Widget/UISelectValue.h"
#include "../../GUI/Widget/UILabel.h"
#include "../../GUI/GUIRender.h"

class TalkSettingPage : public UIPage {
public:
    TalkSettingPage();
    ~TalkSettingPage() override;

    void showPage() override;
    void update() override;
    void render(U8G2* u8g2) override;

    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButton1(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton3(void* context = nullptr) override;
    void onButton4(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton6(void* context = nullptr) override;
    void onButton7(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;
    void onButton9(void* context = nullptr) override;

private:
    enum Field { F_NAME = 0, F_CH = 1, F_MODE = 2, F_COUNT = 3 };
    enum State { ST_LIST = 0, ST_NAME_EDIT = 1 };
    void refreshShow();
    void saveAndReturn();
    void enterNameEdit();
    void exitNameEdit(bool save);
    void setListVisible(bool v);
    void adjustValue(int dir);

    UILabel* titleLabel = nullptr;
    UILabel* nameLabel = nullptr;
    UISelectValue* nameShow = nullptr;   // 设备名显示(选中时T9输入)
    UILabel* chLabel = nullptr;
    UISelectValue* chShow = nullptr;
    UILabel* modeLabel = nullptr;
    UISelectValue* modeShow = nullptr;
    UIInput* nameInput = nullptr;        // T9输入载体(名称字段选中时接管1-9/MENU)
    UINavBar* navBar = nullptr;
    UINavBar* nameNavBar = nullptr;

    int state = ST_LIST;
    int field = F_NAME;
    int channel = 1;
    int defMode = 0;                     // 0 PTT / 1 全双工
};

#endif
