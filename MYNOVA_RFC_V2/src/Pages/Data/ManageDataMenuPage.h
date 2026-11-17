/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef MANAGEDATAMENUPAGE_H
#define MANAGEDATAMENUPAGE_H

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIMenu.h"

/**
 * ManageDataMenuPage - 管理数据分类入口
 * 1.射频数据 -> ManageDataPage(SIG_RF)
 * 2.红外数据 -> ManageDataPage(SIG_IR)
 * 3.TF卡数据 -> FileManagerPage
 */
class ManageDataMenuPage : public UIPage {
public:
    ManageDataMenuPage();
    ~ManageDataMenuPage();

    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;

private:
    void initLayout();
    UIMenu* menu;
};

#endif
