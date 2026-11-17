/* 
* Copyright (c) 2026 Tomosawa 
*/
#include "HomeManagerPage.h"
#include "HomeKeymapEditPage.h"
#include "../../GUI/UIEngine.h"
#include "../../GUI/GUIRender.h"
#include "../../Services/DataStore.h"

extern UIEngine uiEngine;
extern DataStore dataStore;

static String layoutName(uint8_t l) {
    int n = HomeLayoutToKeyCount(l);
    if (n == 1) return "单键";
    return String(n) + "键";
}
static const char* layoutDefaultName(uint8_t l) {
    int n = HomeLayoutToKeyCount(l);
    if (n == 1) return "单键主页";
    if (n == 2) return "双键主页";
    if (n == 3) return "三键主页";
    if (n == 4) return "四键主页";
    if (n == 5) return "五键主页";
    if (n == 6) return "六键主页";
    if (n == 7) return "七键主页";
    if (n == 8) return "八键主页";
    return "九键主页";
}

HomeManagerPage::HomeManagerPage() : UIPage(0,0,SCREEN_WIDTH,SCREEN_HEIGHT) {
    listMenu = new UIMenu(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,5);
    listMenu->bShowBtnTips=true; listMenu->bShowBorder=false; listMenu->bShowTitle=false;
    listMenu->setShowRadio(true);
    listMenu->getNavBar()->setLeftButtonText("返回");
    listMenu->getNavBar()->setRightButtonText("选择");
    addWidget(listMenu);

    actionMenu = new UIMenu(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,5);
    actionMenu->bShowBtnTips=true; actionMenu->bShowBorder=false; actionMenu->bShowTitle=false;
    actionMenu->bVisible=false;
    actionMenu->getNavBar()->setLeftButtonText("返回");
    actionMenu->getNavBar()->setRightButtonText("确定");
    actionMenu->addMenuItem("1. 设为当前");
    actionMenu->addMenuItem("2. 编辑按键");
    actionMenu->addMenuItem("3. 重命名");
    actionMenu->addMenuItem("4. 删除");
    addWidget(actionMenu);

    // 新建向导（1..9键）
    layoutMenu = new UIMenu(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,5);
    layoutMenu->bShowBtnTips=true; layoutMenu->bShowBorder=false; layoutMenu->bShowTitle=false;
    layoutMenu->bVisible=false;
    layoutMenu->getNavBar()->setLeftButtonText("返回");
    layoutMenu->getNavBar()->setRightButtonText("选择");
    addWidget(layoutMenu);

    nameInput = new UIInput(0,0,128,50);
    nameInput->setTextAlign(INPUT_LEFT);
    nameInput->setTitle("主页名称");
    nameInput->bVisible=false;
    addWidget(nameInput);
    inputNavBar = new UINavBar(0,SCREEN_HEIGHT-15,SCREEN_WIDTH,15);
    inputNavBar->setLeftButtonText("返回");
    inputNavBar->setMiddleButtonText("删除");
    inputNavBar->setRightButtonText("确认");
    inputNavBar->bVisible=false;
    addWidget(inputNavBar);

    confirmBox = new UIMessageBox(0,0,SCREEN_WIDTH,SCREEN_HEIGHT);
    confirmBox->setTitle("删除确认");
    confirmBox->setMessage("确定删除该主页?");
    confirmBox->setButtons("取消","删除");
    confirmBox->bVisible=false;
    addWidget(confirmBox);
    confirmNavBar = new UINavBar(0,SCREEN_HEIGHT-15,SCREEN_WIDTH,15);
    confirmNavBar->setLeftButtonText("取消");
    confirmNavBar->setRightButtonText("删除");
    confirmNavBar->bVisible=false;
    addWidget(confirmNavBar);

    tipBox = new UIMessageBox(0,0,SCREEN_WIDTH,SCREEN_HEIGHT);
    tipBox->setTitle("提示");
    tipBox->bVisible=false;
    addWidget(tipBox);

    refreshList();
}

HomeManagerPage::~HomeManagerPage() {}

void HomeManagerPage::refreshList() {
    int keepRadio = listMenu->bShowRadio;
    listMenu->clear();
    listMenu->setShowRadio(keepRadio);
    profiles = dataStore.ListHomeProfiles();
    int active = dataStore.GetActiveHomeProfile();
    int activeIdx = -1;
    for (auto& p : profiles) {
        String s = p.name + " [" + layoutName(p.layout) + "]";
        listMenu->addMenuItem(s);
        if (p.id == active) activeIdx = (int)listMenu->itemCount() - 1;
    }
    // 末项：+ 新建主页（无 radio，始终可见，便于直接创建）
    if ((int)profiles.size() < HOME_PROFILE_MAX) {
        listMenu->addMenuItem("+ 新建主页");
    } else if (profiles.empty()) {
        listMenu->addMenuItem("  (无主页)");
    }
    listMenu->setRadioSelected(activeIdx);
}

void HomeManagerPage::showPage() {
    UIPage::showPage();
    refreshList();
    state = STATE_LIST;
    listMenu->bVisible=true; actionMenu->bVisible=false;
    layoutMenu->bVisible=false;
    nameInput->bVisible=false; inputNavBar->bVisible=false;
    confirmBox->bVisible=false; confirmNavBar->bVisible=false;
    tipBox->bVisible=false;
    createStage = CREATE_PICK_LAYOUT;
}

void HomeManagerPage::render(U8G2* u8g2) { UIPage::render(u8g2); if (tipBox->bVisible) tipBox->update(); if (confirmBox->bVisible) confirmBox->update(); }

void HomeManagerPage::enterAction() {
    int idx = listMenu->menuSel;
    // 末项为 + 新建主页
    if (idx == (int)profiles.size() && (int)profiles.size() < HOME_PROFILE_MAX) {
        // 直接进入新建向导，无需经过二级菜单
        enterCreateLayout();
        return;
    }
    if (profiles.empty() || idx < 0 || idx >= (int)profiles.size()) return;
    listMenu->bVisible=false;
    actionMenu->bVisible=true;
    actionMenu->clear();
    actionMenu->addMenuItem("1. 设为当前");
    actionMenu->addMenuItem("2. 编辑按键");
    actionMenu->addMenuItem("3. 重命名");
    actionMenu->addMenuItem("4. 删除");
    state = STATE_ACTION;
}

void HomeManagerPage::doSetActive() {
    int idx = listMenu->menuSel;
    if (idx < 0 || idx >= (int)profiles.size()) return;
    int pid = profiles[idx].id;
    dataStore.SetActiveHomeProfile(pid);
    actionMenu->bVisible=false; listMenu->bVisible=true;
    refreshList();
    state = STATE_LIST;
    showTip("已切换");
}

void HomeManagerPage::doEditKeys() {
    int idx = listMenu->menuSel;
    if (idx < 0 || idx >= (int)profiles.size()) return;
    int pid = profiles[idx].id;
    uiEngine.navigateTo(new HomeKeymapEditPage(pid));
}

void HomeManagerPage::enterRename() {
    int idx = listMenu->menuSel;
    if (idx < 0 || idx >= (int)profiles.size()) return;
    renameProfileId = profiles[idx].id;
    actionMenu->bVisible=false;
    nameInput->setTitle("新名称");
    nameInput->bVisible=true; inputNavBar->bVisible=true;
    nameInput->setText(profiles[idx].name);
    state = STATE_RENAME;
}

void HomeManagerPage::doRename() {
    String n = nameInput->getText(); n.trim();
    if (n.length()==0) { showTip("名称不能为空"); return; }
    HomeProfile p; if (!dataStore.LoadHomeProfile(renameProfileId, p)) { showTip("读取失败"); return; }
    p.name = n.substring(0, HOME_NAME_MAX);
    dataStore.SaveHomeProfile(p);
    nameInput->bVisible=false; inputNavBar->bVisible=false;
    listMenu->bVisible=true;
    refreshList();
    state = STATE_LIST;
}

void HomeManagerPage::enterDeleteConfirm() {
    if ((int)profiles.size() <= 1) { showTip("至少保留一个主页"); return; }
    actionMenu->bVisible=false;
    confirmBox->show(MSGBOX_ANIME_ZOOM_CENTER, 250);
    confirmNavBar->bVisible=true;
    state = STATE_DELETE_CONFIRM;
}

void HomeManagerPage::doDelete() {
    int idx = listMenu->menuSel;
    if (idx < 0 || idx >= (int)profiles.size()) return;
    int pid = profiles[idx].id;
    confirmBox->hide(); confirmNavBar->bVisible=false;
    dataStore.DeleteHomeProfile(pid);
    refreshList();
    listMenu->bVisible=true;
    state = STATE_LIST;
}

void HomeManagerPage::enterCreateLayout() {
    // 来自 list 的 +新建 或 action 的入口，都要隐藏上层菜单
    listMenu->bVisible=false;
    actionMenu->bVisible=false;
    layoutMenu->bVisible=true;
    layoutMenu->clear();
    layoutMenu->addMenuItem("1. 单键");
    layoutMenu->addMenuItem("2. 双键");
    layoutMenu->addMenuItem("3. 三键");
    layoutMenu->addMenuItem("4. 四键");
    layoutMenu->addMenuItem("5. 五键");
    layoutMenu->addMenuItem("6. 六键");
    layoutMenu->addMenuItem("7. 七键");
    layoutMenu->addMenuItem("8. 八键");
    layoutMenu->addMenuItem("9. 九键");
    createStage = CREATE_PICK_LAYOUT;
    state = STATE_CREATE;
}

void HomeManagerPage::enterCreateName() {
    int sel = layoutMenu->menuSel;
    uint8_t layout = HomeLayoutFromKeyCount(sel + 1); // 0..8
    createLayout = layout;
    layoutMenu->bVisible=false;
    nameInput->setTitle("主页名称");
    nameInput->bVisible=true; inputNavBar->bVisible=true;
    nameInput->setText(layoutDefaultName(createLayout));
    createStage = CREATE_INPUT_NAME;
}

void HomeManagerPage::doCreate() {
    if (dataStore.GetHomeProfileCount() >= HOME_PROFILE_MAX) {
        nameInput->bVisible=false; inputNavBar->bVisible=false;
        showTip("已达上限(8个)");
        return;
    }
    String name = nameInput->getText(); name.trim();
    if (name.length()==0) name = layoutDefaultName(createLayout);
    int nid = dataStore.CreateHomeProfile(name, createLayout);
    if (nid < 0) {
        nameInput->bVisible=false; inputNavBar->bVisible=false;
        showTip("创建失败");
        return;
    }
    nameInput->bVisible=false; inputNavBar->bVisible=false;
    layoutMenu->bVisible=false;
    refreshList();
    listMenu->bVisible=true;
    state = STATE_LIST;
    uiEngine.navigateTo(new HomeKeymapEditPage(nid));
}

void HomeManagerPage::showTip(const String& msg) {
    tipBox->setTitle("提示");
    tipBox->setMessage(msg);
    tipBox->setButtons("返回","");
    tipBox->show(MSGBOX_ANIME_BOUNCE_IN, 200);
    tipBox->setAutoClose(1200);
    state = STATE_TIP;
}

void HomeManagerPage::onButtonBack(void* context) {
    switch(state){
        case STATE_LIST:
            listMenu->getNavBar()->showLeftBlink(1,80,80,[this](){ uiEngine.navigateBack(); });
            break;
        case STATE_ACTION:
            actionMenu->getNavBar()->showLeftBlink(1,80,80,[this](){
                actionMenu->bVisible=false; listMenu->bVisible=true; state=STATE_LIST;
            });
            break;
        case STATE_CREATE:
            if (createStage == CREATE_PICK_LAYOUT) {
                layoutMenu->getNavBar()->showLeftBlink(1,80,80,[this](){
                    layoutMenu->bVisible=false; listMenu->bVisible=true; state=STATE_LIST;
                });
            } else {
                inputNavBar->showLeftBlink(1,80,80,[this](){
                    nameInput->bVisible=false; inputNavBar->bVisible=false;
                    layoutMenu->bVisible=true; createStage=CREATE_PICK_LAYOUT;
                });
            }
            break;
        case STATE_RENAME:
            inputNavBar->showLeftBlink(1,80,80,[this](){
                nameInput->bVisible=false; inputNavBar->bVisible=false;
                actionMenu->bVisible=true; state=STATE_ACTION;
            });
            break;
        case STATE_DELETE_CONFIRM:
            confirmNavBar->showLeftBlink(1,80,80,[this](){
                confirmBox->hide(true); confirmNavBar->bVisible=false;
                actionMenu->bVisible=true; state=STATE_ACTION;
            });
            break;
        case STATE_TIP:
            tipBox->hide(true); tipBox->cancelAutoClose();
            layoutMenu->bVisible=false; nameInput->bVisible=false; inputNavBar->bVisible=false;
            refreshList();
            listMenu->bVisible=true; actionMenu->bVisible=false;
            state=STATE_LIST;
            break;
    }
}
void HomeManagerPage::onButtonEnter(void* context) {
    switch(state){
        case STATE_LIST: {
            listMenu->getNavBar()->showRightBlink(1,80,80,[this](){ enterAction(); });
            break;
        }
        case STATE_ACTION: {
            int sel=actionMenu->menuSel;
            actionMenu->getNavBar()->showRightBlink(1,80,80,[this,sel](){
                if (sel==0) doSetActive();
                else if (sel==1) doEditKeys();
                else if (sel==2) enterRename();
                else if (sel==3) enterDeleteConfirm();
            });
            break;
        }
        case STATE_CREATE: {
            if (createStage == CREATE_PICK_LAYOUT) {
                layoutMenu->getNavBar()->showRightBlink(1,80,80,[this](){ enterCreateName(); });
            } else {
                inputNavBar->showRightBlink(1,80,80,[this](){ doCreate(); });
            }
            break;
        }
        case STATE_RENAME:
            inputNavBar->showRightBlink(1,80,80,[this](){ doRename(); });
            break;
        case STATE_DELETE_CONFIRM:
            confirmNavBar->showRightBlink(1,80,80,[this](){ doDelete(); });
            break;
        case STATE_TIP:
            tipBox->hide(true); tipBox->cancelAutoClose();
            layoutMenu->bVisible=false; nameInput->bVisible=false; inputNavBar->bVisible=false;
            refreshList();
            listMenu->bVisible=true; actionMenu->bVisible=false;
            state=STATE_LIST;
            break;
    }
}
void HomeManagerPage::onButtonMenu(void* context) {
    if (state==STATE_LIST) listMenu->moveDown();
    else if (state==STATE_ACTION) actionMenu->moveDown();
    else if (state==STATE_CREATE) {
        if (createStage==CREATE_PICK_LAYOUT) layoutMenu->moveDown();
        else nameInput->deleteChar();
    }
    else if (state==STATE_RENAME) nameInput->deleteChar();
}
void HomeManagerPage::onButton1(void* context) {
    if (state==STATE_CREATE && createStage==CREATE_INPUT_NAME) nameInput->onButtonPress(1);
    else if (state==STATE_RENAME) nameInput->onButtonPress(1);
    else if (state==STATE_LIST) listMenu->moveUp();
    else if (state==STATE_ACTION) actionMenu->moveUp();
    else if (state==STATE_CREATE && createStage==CREATE_PICK_LAYOUT) layoutMenu->moveUp();
}
void HomeManagerPage::onButton2(void* context) {
    if (state==STATE_CREATE && createStage==CREATE_INPUT_NAME) nameInput->onButtonPress(2);
    else if (state==STATE_RENAME) nameInput->onButtonPress(2);
    else if (state==STATE_LIST) listMenu->moveUp();
    else if (state==STATE_ACTION) actionMenu->moveUp();
    else if (state==STATE_CREATE && createStage==CREATE_PICK_LAYOUT) layoutMenu->moveUp();
}
void HomeManagerPage::onButton3(void* context) {
    if (state==STATE_CREATE && createStage==CREATE_INPUT_NAME) nameInput->onButtonPress(3);
    else if (state==STATE_RENAME) nameInput->onButtonPress(3);
    else if (state==STATE_CREATE && createStage==CREATE_PICK_LAYOUT) layoutMenu->moveDown();
}
void HomeManagerPage::onButton4(void* context) {
    if (state==STATE_CREATE && createStage==CREATE_INPUT_NAME) nameInput->onButtonPress(4);
    else if (state==STATE_RENAME) nameInput->onButtonPress(4);
    else if (state==STATE_CREATE && createStage==CREATE_PICK_LAYOUT) layoutMenu->moveDown();
}
void HomeManagerPage::onButton5(void* context) {
    if (state==STATE_CREATE && createStage==CREATE_INPUT_NAME) nameInput->onButtonPress(5);
    else if (state==STATE_RENAME) nameInput->onButtonPress(5);
    else onButtonEnter(context);
}
void HomeManagerPage::onButton6(void* context) {
    if (state==STATE_CREATE && createStage==CREATE_INPUT_NAME) nameInput->onButtonPress(6);
    else if (state==STATE_RENAME) nameInput->onButtonPress(6);
    else if (state==STATE_CREATE && createStage==CREATE_PICK_LAYOUT) layoutMenu->moveDown();
}
void HomeManagerPage::onButton7(void* context) {
    if (state==STATE_CREATE && createStage==CREATE_INPUT_NAME) nameInput->onButtonPress(7);
    else if (state==STATE_RENAME) nameInput->onButtonPress(7);
    else if (state==STATE_CREATE && createStage==CREATE_PICK_LAYOUT) layoutMenu->moveDown();
}
void HomeManagerPage::onButton8(void* context) {
    if (state==STATE_LIST) listMenu->moveDown();
    else if (state==STATE_ACTION) actionMenu->moveDown();
    else if (state==STATE_CREATE) {
        if (createStage==CREATE_PICK_LAYOUT) layoutMenu->moveDown();
        else nameInput->onButtonPress(8);
    }
    else if (state==STATE_RENAME) nameInput->onButtonPress(8);
}
void HomeManagerPage::onButton9(void* context) {
    if (state==STATE_CREATE && createStage==CREATE_INPUT_NAME) nameInput->onButtonPress(9);
    else if (state==STATE_RENAME) nameInput->onButtonPress(9);
    else if (state==STATE_CREATE && createStage==CREATE_PICK_LAYOUT) layoutMenu->moveDown();
}
