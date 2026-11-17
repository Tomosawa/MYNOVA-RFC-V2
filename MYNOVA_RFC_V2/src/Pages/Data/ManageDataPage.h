/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef MANAGEDATAPAGE_H
#define MANAGEDATAPAGE_H

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIMenu.h"
#include "../../GUI/Widget/UIMessageBox.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../GUI/Widget/UIInput.h"
#include "../../Proto/SignalData.h"
#include "../../Services/DataStore.h"
#include <vector>

/**
 * ManageDataPage - 数据管理页面（按类型隔离）
 *  RF: 列表(100) -> 动作[修改/删除/绑定到主页] -> 修改走EditDataPage
 *  IR: 列表(100) -> 动作[查看详情/重命名/重新学习/删除/绑定到主页]
 *      空槽 -> RF新建(EditDataPage) / IR新建(IRLearnPage带suggestedSlot)
 *  绑定到主页: 选主页 -> 选键位 -> 写入 HomeProfile.slotMap
 */
class ManageDataPage : public UIPage {
public:
    explicit ManageDataPage(SignalKind kind = SIG_RF);
    ~ManageDataPage();
    
    void render(U8G2* u8g2) override;
    void update();
    void showPage() override;
    
    void onButtonBack(void* context) override;
    void onButtonEnter(void* context) override;
    void onButtonMenu(void* context) override;
    void onButton1(void* context) override;
    void onButton2(void* context) override;
    void onButton3(void* context) override;
    void onButton4(void* context) override;
    void onButton5(void* context) override;
    void onButton6(void* context) override;
    void onButton7(void* context) override;
    void onButton8(void* context) override;
    void onButton9(void* context) override;
    
private:
    enum ManageState {
        STATE_SELECT_DATA,
        STATE_SELECT_ACTION,
        STATE_DELETE_CONFIRM,
        STATE_BIND_HOME,
        STATE_BIND_KEY,
        STATE_RENAME,
        STATE_TIP
    };
    
    void initLayout();
    void showActionMenu(int dataIndex);
    void rebuildActionMenu();
    void showDeleteConfirm();
    void deleteData();
    void editData();
    void createNewData();
    void refreshDataList();

    // IR / 绑定相关
    void showIRViewer();
    void showRename();
    void doRename();
    void startIRLearn();
    void showBindHomePick();
    void showBindKeyPick();
    void doBindToKey(int keyMenuIndex);
    void showTip(const String& msg);

    SignalKind dataKind;
    int slotCount;
    ManageState currentState;
    int selectedDataIndex;
    bool hasData;
    bool selectedIsIR;
    
    // 主页绑定缓存
    std::vector<HomeProfile> bindHomeProfiles;
    int bindPickedProfileId = -1;
    int bindPickedMenuIndex = -1;

    // UI组件
    UIMenu* dataListMenu;
    UIMenu* actionMenu;
    UIMessageBox* deleteConfirmBox;
    UINavBar* deleteNavBar;

    UIMenu* homePickMenu;
    UIMenu* keyPickMenu;
    UIInput* renameInput;
    UINavBar* renameNavBar;
    UIMessageBox* tipBox;
};

#endif
