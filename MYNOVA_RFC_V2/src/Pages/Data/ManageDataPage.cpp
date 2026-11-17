/* 
* Copyright (c) 2026 Tomosawa 
*/

#include "ManageDataPage.h"
#include "EditDataPage.h"
#include "../Ir/IRLearnPage.h"
#include "../Ir/IRDataViewerPage.h"
#include "../../GUI/UIEngine.h"
#include "../../GUI/Animation/MessageBoxAnimation.h"
#include "../../Services/DataStore.h"
#include "../../GUI/GUIRender.h"

extern UIEngine uiEngine;
extern DataStore dataStore;

ManageDataPage::ManageDataPage(SignalKind kind) : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT),
    dataKind(kind),
    slotCount(100),
    currentState(STATE_SELECT_DATA),
    selectedDataIndex(0),
    hasData(false),
    selectedIsIR(kind == SIG_IR) {
    initLayout();
}

ManageDataPage::~ManageDataPage() {}

void ManageDataPage::initLayout() {
    dataListMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    dataListMenu->titleText = (dataKind == SIG_IR) ? "红外数据" : "射频数据";
    dataListMenu->bShowBtnTips = true;
    dataListMenu->bShowBorder = false;
    dataListMenu->bShowTitle = false;
    dataListMenu->bVisible = true;
    dataListMenu->getNavBar()->setLeftButtonText("返回");
    dataListMenu->getNavBar()->setRightButtonText("选择");
    for (int i = 1; i <= slotCount; i++) {
        String itemName = String(i) + ". ";
        SignalEntry entry = dataStore.ReadSignal(dataKind, i);
        if (entry.name.length() > 0) itemName += entry.name;
        else itemName += "----------";
        dataListMenu->addMenuItem(itemName);
    }
    addWidget(dataListMenu);

    actionMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    actionMenu->titleText = "选择操作";
    actionMenu->bShowBtnTips = true;
    actionMenu->bShowBorder = false;
    actionMenu->bShowTitle = false;
    actionMenu->bVisible = false;
    actionMenu->getNavBar()->setLeftButtonText("返回");
    actionMenu->getNavBar()->setRightButtonText("确认");
    addWidget(actionMenu);

    deleteConfirmBox = new UIMessageBox(-1, -1, 100, 50);
    deleteConfirmBox->setDefaultPosition(SCREEN_WIDTH, SCREEN_HEIGHT, 15);
    deleteConfirmBox->setMessage("确认删除该数据？");
    deleteConfirmBox->setTitle("删除确认");
    deleteConfirmBox->bShowButtons = false;
    addWidget(deleteConfirmBox);

    deleteNavBar = new UINavBar(0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, 15);
    deleteNavBar->setLeftButtonText("取消");
    deleteNavBar->setRightButtonText("删除");
    deleteNavBar->bVisible = false;
    addWidget(deleteNavBar);

    homePickMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    homePickMenu->bShowBtnTips = true;
    homePickMenu->bShowBorder = false;
    homePickMenu->bShowTitle = false;
    homePickMenu->bVisible = false;
    homePickMenu->getNavBar()->setLeftButtonText("返回");
    homePickMenu->getNavBar()->setRightButtonText("确定");
    addWidget(homePickMenu);

    keyPickMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    keyPickMenu->bShowBtnTips = true;
    keyPickMenu->bShowBorder = false;
    keyPickMenu->bShowTitle = false;
    keyPickMenu->bVisible = false;
    keyPickMenu->getNavBar()->setLeftButtonText("返回");
    keyPickMenu->getNavBar()->setRightButtonText("绑定");
    addWidget(keyPickMenu);

    renameInput = new UIInput(0, 0, 128, 50);
    renameInput->setTextAlign(INPUT_LEFT);
    renameInput->setTitle("新名称");
    renameInput->bVisible = false;
    addWidget(renameInput);
    renameNavBar = new UINavBar(0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, 15);
    renameNavBar->setLeftButtonText("返回");
    renameNavBar->setMiddleButtonText("删除");
    renameNavBar->setRightButtonText("保存");
    renameNavBar->bVisible = false;
    addWidget(renameNavBar);

    tipBox = new UIMessageBox(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    tipBox->setTitle("提示");
    tipBox->bVisible = false;
    addWidget(tipBox);
}

void ManageDataPage::rebuildActionMenu() {
    actionMenu->clear();
    if (dataKind == SIG_RF) {
        actionMenu->addMenuItem("1. 修改数据");
        actionMenu->addMenuItem("2. 删除数据");
        actionMenu->addMenuItem("3. 绑定到主页");
    } else {
        actionMenu->addMenuItem("1. 查看详情");
        actionMenu->addMenuItem("2. 重命名");
        actionMenu->addMenuItem("3. 重新学习");
        actionMenu->addMenuItem("4. 删除数据");
        actionMenu->addMenuItem("5. 绑定到主页");
    }
}

void ManageDataPage::showActionMenu(int dataIndex) {
    selectedDataIndex = dataIndex;
    SignalEntry entry = dataStore.ReadSignal(dataKind, dataIndex);
    hasData = (entry.name.length() > 0);
    selectedIsIR = (dataKind == SIG_IR);
    if (hasData) {
        rebuildActionMenu();
        dataListMenu->bVisible = false;
        actionMenu->bVisible = true;
        currentState = STATE_SELECT_ACTION;
    } else {
        createNewData();
    }
}

void ManageDataPage::showDeleteConfirm() {
    actionMenu->bVisible = false;
    homePickMenu->bVisible = false;
    keyPickMenu->bVisible = false;
    deleteConfirmBox->show(MSGBOX_ANIME_ZOOM_CENTER, 300);
    deleteNavBar->bVisible = true;
    currentState = STATE_DELETE_CONFIRM;
}

void ManageDataPage::deleteData() {
    dataStore.DeleteSignal(dataKind, selectedDataIndex);
    refreshDataList();
    deleteConfirmBox->hide();
    deleteNavBar->bVisible = false;
    dataListMenu->bVisible = true;
    currentState = STATE_SELECT_DATA;
}

void ManageDataPage::editData() {
    EditDataPage* editPage = new EditDataPage(selectedDataIndex, false);
    uiEngine.navigateTo(editPage);
}

void ManageDataPage::createNewData() {
    if (dataKind == SIG_IR) {
        IRLearnPage* p = new IRLearnPage(selectedDataIndex);
        uiEngine.navigateTo(p);
    } else {
        EditDataPage* editPage = new EditDataPage(selectedDataIndex, true);
        uiEngine.navigateTo(editPage);
    }
}

void ManageDataPage::refreshDataList() {
    for (int i = slotCount - 1; i >= 0; i--) dataListMenu->removeMenuItem(i);
    for (int i = 1; i <= slotCount; i++) {
        String itemName = String(i) + ". ";
        SignalEntry entry = dataStore.ReadSignal(dataKind, i);
        if (entry.name.length() > 0) itemName += entry.name;
        else itemName += "----------";
        dataListMenu->addMenuItem(itemName);
    }
}

void ManageDataPage::showIRViewer() {
    uiEngine.navigateTo(new IRDataViewerPage(selectedDataIndex));
}

void ManageDataPage::showRename() {
    SignalEntry e = dataStore.ReadSignal(dataKind, selectedDataIndex);
    actionMenu->bVisible = false;
    renameInput->setTitle("新名称");
    renameInput->setText(e.name);
    renameInput->bVisible = true;
    renameNavBar->bVisible = true;
    currentState = STATE_RENAME;
}

void ManageDataPage::doRename() {
    String n = renameInput->getText(); n.trim();
    if (n.length() == 0) { showTip("名称不能为空"); return; }
    bool ok;
    if (dataKind == SIG_IR) {
        ok = dataStore.RenameIRSignal(selectedDataIndex, n);
    } else {
        DataStore::StorageGuard storage(dataStore);
        if (!storage) { showTip("保存失败"); return; }
        SignalEntry entry = dataStore.ReadSignal(dataKind, selectedDataIndex);
        if (entry.name.isEmpty()) { showTip("读取失败"); return; }
        entry.name = n;
        ok = dataStore.SaveSignal(selectedDataIndex, entry);
    }
    if (!ok) { showTip("保存失败"); return; }
    renameInput->bVisible = false;
    renameNavBar->bVisible = false;
    refreshDataList();
    // 返回动作菜单并刷新选中项
    rebuildActionMenu();
    actionMenu->bVisible = true;
    currentState = STATE_SELECT_ACTION;
}

void ManageDataPage::startIRLearn() {
    IRLearnPage* p = new IRLearnPage(selectedDataIndex);
    uiEngine.navigateTo(p);
}

void ManageDataPage::showBindHomePick() {
    bindHomeProfiles = dataStore.ListHomeProfiles();
    if (bindHomeProfiles.empty()) { showTip("请先创建主页"); return; }
    homePickMenu->clear();
    for (auto &ph : bindHomeProfiles) {
        String label = ph.name + " [" + String(HomeLayoutToKeyCount(ph.layout)) + "键]";
        homePickMenu->addMenuItem(label);
    }
    actionMenu->bVisible = false;
    homePickMenu->bVisible = true;
    currentState = STATE_BIND_HOME;
}

void ManageDataPage::showBindKeyPick() {
    int hIdx = homePickMenu->menuSel;
    if (hIdx < 0 || hIdx >= (int)bindHomeProfiles.size()) return;
    bindPickedProfileId = bindHomeProfiles[hIdx].id;
    bindPickedMenuIndex = hIdx;
    HomeProfile prof;
    if (!dataStore.LoadHomeProfile(bindPickedProfileId, prof)) { showTip("读取主页失败"); return; }
    keyPickMenu->clear();
    int n = HomeLayoutToKeyCount(prof.layout);
    for (int k = 1; k <= n; k++) {
        int slot = prof.slotMap[k-1];
        String label = "键" + String(k) + ": ";
        if (prof.layout == HOME_LAYOUT_KEY1 && k == 1) label = "键1(单键): ";
        SignalEntry e;
        if (SlotIsIR(slot)) e = dataStore.ReadSignal(SIG_IR, SlotToIndex(slot));
        else if (SlotIsRF(slot)) e = dataStore.ReadSignal(SIG_RF, slot);
        else if (slot != 0) e = dataStore.ReadSignal(slot);
        if (slot != 0 && e.name.length() > 0) {
            label += (e.kind==SIG_IR?"[IR]":"[RF]") + e.name;
            label += " (#" + String(slot) + ")";
        } else if (slot != 0) {
            label += "(空槽 #" + String(slot) + ")";
        } else label += "----------";
        // 高亮当前待绑定的数据对应的键（若已绑定）
        keyPickMenu->addMenuItem(label);
    }
    // 预选中：若该主页已绑定此数据，则高亮该键
    int wantSlot = KindIndexToSlot(dataKind, selectedDataIndex);
    int preSel = 0;
    for (int k = 1; k <= n; k++) if (prof.slotMap[k-1] == wantSlot) { preSel = k-1; break; }
    keyPickMenu->menuSel = preSel;
    keyPickMenu->menuStart = 0;
    keyPickMenu->menuPos = preSel;
    int lines = keyPickMenu->menuLines;
    if (keyPickMenu->bShowTitle) lines--;
    if (keyPickMenu->bShowBtnTips) lines--;
    if (keyPickMenu->menuPos >= lines) {
        keyPickMenu->menuStart = preSel - lines + 1;
        keyPickMenu->menuPos = lines -1;
    }
    homePickMenu->bVisible = false;
    keyPickMenu->bVisible = true;
    currentState = STATE_BIND_KEY;
}

void ManageDataPage::doBindToKey(int keyMenuIndex) {
    HomeProfile prof;
    if (!dataStore.LoadHomeProfile(bindPickedProfileId, prof)) { showTip("读取失败"); return; }
    int n = HomeLayoutToKeyCount(prof.layout);
    // keyMenuIndex 对应 validKeys 的第 keyMenuIndex 个物理键
    int physKey = keyMenuIndex + 1; // 因 getValidKeys 就是 1..n 连续
    if (physKey < 1 || physKey > n) return;
    int slot = KindIndexToSlot(dataKind, selectedDataIndex);
    prof.slotMap[physKey-1] = (int16_t)slot;
    bool ok = dataStore.SaveHomeProfile(prof);
    keyPickMenu->bVisible = false;
    homePickMenu->bVisible = false;
    if (ok) {
        showTip("已绑定到 " + prof.name + "/键" + String(physKey));
    } else {
        showTip("绑定失败");
    }
}

void ManageDataPage::showTip(const String& msg) {
    // 隐藏可能的前景菜单
    homePickMenu->bVisible = false;
    keyPickMenu->bVisible = false;
    renameInput->bVisible = false;
    renameNavBar->bVisible = false;
    deleteConfirmBox->hide();
    deleteNavBar->bVisible = false;
    actionMenu->bVisible = false;
    tipBox->setTitle("提示");
    tipBox->setMessage(msg);
    tipBox->setButtons("返回","");
    tipBox->show(MSGBOX_ANIME_BOUNCE_IN, 200);
    tipBox->setAutoClose(1200);
    currentState = STATE_TIP;
}

void ManageDataPage::render(U8G2* u8g2) {
    UIPage::render(u8g2);
    update();
}

void ManageDataPage::update() {
    deleteConfirmBox->update();
    if (tipBox->bVisible) tipBox->update();
    if (currentState == STATE_RENAME) renameInput->update();
}

void ManageDataPage::showPage() {
    UIPage::showPage();
    refreshDataList();
    dataListMenu->bVisible = true;
    actionMenu->bVisible = false;
    homePickMenu->bVisible = false;
    keyPickMenu->bVisible = false;
    renameInput->bVisible = false;
    renameNavBar->bVisible = false;
    deleteConfirmBox->hide();
    deleteNavBar->bVisible = false;
    tipBox->hide(true);
    tipBox->cancelAutoClose();
    currentState = STATE_SELECT_DATA;
}

void ManageDataPage::onButtonBack(void* context) {
    switch (currentState) {
        case STATE_SELECT_DATA:
            dataListMenu->getNavBar()->showLeftBlink(1, 80, 80, [this]() { uiEngine.navigateBack(); });
            break;
        case STATE_SELECT_ACTION:
            actionMenu->getNavBar()->showLeftBlink(1, 80, 80, [this]() {
                actionMenu->bVisible = false;
                dataListMenu->bVisible = true;
                currentState = STATE_SELECT_DATA;
            });
            break;
        case STATE_DELETE_CONFIRM:
            deleteNavBar->showLeftBlink(1, 80, 80, [this]() {
                deleteConfirmBox->hide();
                deleteNavBar->bVisible = false;
                actionMenu->bVisible = true;
                currentState = STATE_SELECT_ACTION;
            });
            break;
        case STATE_BIND_HOME:
            homePickMenu->getNavBar()->showLeftBlink(1, 80, 80, [this]() {
                homePickMenu->bVisible = false;
                actionMenu->bVisible = true;
                currentState = STATE_SELECT_ACTION;
            });
            break;
        case STATE_BIND_KEY:
            keyPickMenu->getNavBar()->showLeftBlink(1, 80, 80, [this]() {
                keyPickMenu->bVisible = false;
                homePickMenu->bVisible = true;
                currentState = STATE_BIND_HOME;
            });
            break;
        case STATE_RENAME:
            renameNavBar->showLeftBlink(1, 80, 80, [this]() {
                renameInput->bVisible = false;
                renameNavBar->bVisible = false;
                actionMenu->bVisible = true;
                currentState = STATE_SELECT_ACTION;
            });
            break;
        case STATE_TIP:
            tipBox->hide(true); tipBox->cancelAutoClose();
            refreshDataList();
            // 回到列表或动作视情况；统一回列表最稳妥
            dataListMenu->bVisible = true;
            actionMenu->bVisible = false;
            homePickMenu->bVisible = false;
            keyPickMenu->bVisible = false;
            currentState = STATE_SELECT_DATA;
            break;
    }
}

void ManageDataPage::onButtonEnter(void* context) {
    switch (currentState) {
        case STATE_SELECT_DATA: {
            int selectedIndex = dataListMenu->menuSel + 1;
            dataListMenu->getNavBar()->showRightBlink(1, 80, 80, [this, selectedIndex]() { showActionMenu(selectedIndex); });
            break;
        }
        case STATE_SELECT_ACTION: {
            int actionIndex = actionMenu->menuSel;
            actionMenu->getNavBar()->showRightBlink(1, 80, 80, [this, actionIndex]() {
                if (dataKind == SIG_RF) {
                    if (actionIndex == 0) editData();
                    else if (actionIndex == 1) showDeleteConfirm();
                    else if (actionIndex == 2) showBindHomePick();
                } else {
                    if (actionIndex == 0) showIRViewer();
                    else if (actionIndex == 1) showRename();
                    else if (actionIndex == 2) startIRLearn();
                    else if (actionIndex == 3) showDeleteConfirm();
                    else if (actionIndex == 4) showBindHomePick();
                }
            });
            break;
        }
        case STATE_DELETE_CONFIRM:
            deleteNavBar->showRightBlink(1, 80, 80, [this]() { deleteData(); });
            break;
        case STATE_BIND_HOME: {
            homePickMenu->getNavBar()->showRightBlink(1, 80, 80, [this]() { showBindKeyPick(); });
            break;
        }
        case STATE_BIND_KEY: {
            int kIdx = keyPickMenu->menuSel;
            keyPickMenu->getNavBar()->showRightBlink(1, 80, 80, [this, kIdx]() { doBindToKey(kIdx); });
            break;
        }
        case STATE_RENAME:
            renameNavBar->showRightBlink(1, 80, 80, [this]() { doRename(); });
            break;
        case STATE_TIP:
            tipBox->hide(true); tipBox->cancelAutoClose();
            refreshDataList();
            dataListMenu->bVisible = true;
            actionMenu->bVisible = false;
            homePickMenu->bVisible = false;
            keyPickMenu->bVisible = false;
            currentState = STATE_SELECT_DATA;
            break;
    }
}

void ManageDataPage::onButtonMenu(void* context) {
    if (currentState == STATE_SELECT_DATA) dataListMenu->moveDown();
    else if (currentState == STATE_SELECT_ACTION) actionMenu->moveDown();
    else if (currentState == STATE_BIND_HOME) homePickMenu->moveDown();
    else if (currentState == STATE_BIND_KEY) keyPickMenu->moveDown();
    else if (currentState == STATE_RENAME) renameInput->deleteChar();
}

void ManageDataPage::onButton1(void* context) {
    if (currentState == STATE_SELECT_DATA) dataListMenu->moveUp();
    else if (currentState == STATE_SELECT_ACTION) actionMenu->moveUp();
    else if (currentState == STATE_BIND_HOME) homePickMenu->moveUp();
    else if (currentState == STATE_BIND_KEY) keyPickMenu->moveUp();
    else if (currentState == STATE_RENAME) renameInput->onButtonPress(1);
}

void ManageDataPage::onButton2(void* context) {
    if (currentState == STATE_SELECT_DATA) dataListMenu->moveUp();
    else if (currentState == STATE_SELECT_ACTION) actionMenu->moveUp();
    else if (currentState == STATE_BIND_HOME) homePickMenu->moveUp();
    else if (currentState == STATE_BIND_KEY) keyPickMenu->moveUp();
    else if (currentState == STATE_RENAME) renameInput->onButtonPress(2);
}

void ManageDataPage::onButton3(void* context) {
    if (currentState == STATE_RENAME) renameInput->onButtonPress(3);
}

void ManageDataPage::onButton4(void* context) {
    if (currentState == STATE_RENAME) renameInput->onButtonPress(4);
}

void ManageDataPage::onButton5(void* context) {
    if (currentState == STATE_RENAME) renameInput->onButtonPress(5);
    else onButtonEnter(context);
}

void ManageDataPage::onButton6(void* context) {
    if (currentState == STATE_RENAME) renameInput->onButtonPress(6);
}

void ManageDataPage::onButton7(void* context) {
    if (currentState == STATE_RENAME) renameInput->onButtonPress(7);
}

void ManageDataPage::onButton8(void* context) {
    if (currentState == STATE_SELECT_DATA) dataListMenu->moveDown();
    else if (currentState == STATE_SELECT_ACTION) actionMenu->moveDown();
    else if (currentState == STATE_BIND_HOME) homePickMenu->moveDown();
    else if (currentState == STATE_BIND_KEY) keyPickMenu->moveDown();
    else if (currentState == STATE_RENAME) renameInput->onButtonPress(8);
}

void ManageDataPage::onButton9(void* context) {
    if (currentState == STATE_RENAME) renameInput->onButtonPress(9);
}
