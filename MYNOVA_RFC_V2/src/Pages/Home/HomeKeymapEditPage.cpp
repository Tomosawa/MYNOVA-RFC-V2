/* 
* Copyright (c) 2026 Tomosawa 
*/
#include "HomeKeymapEditPage.h"
#include "../../GUI/UIEngine.h"
#include "../../GUI/GUIRender.h"
#include "../../Services/DataStore.h"

extern UIEngine uiEngine;
extern DataStore dataStore;

HomeKeymapEditPage::HomeKeymapEditPage(int pid) : UIPage(0,0,SCREEN_WIDTH,SCREEN_HEIGHT), profileId(pid) {
    if (!dataStore.LoadHomeProfile(profileId, profile)) {
        auto list = dataStore.ListHomeProfiles();
        if (!list.empty()) { profile = list[0]; profileId = profile.id; }
    }

    keyMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    keyMenu->bShowBtnTips = true; keyMenu->bShowBorder = false; keyMenu->bShowTitle = false;
    keyMenu->getNavBar()->setLeftButtonText("返回");
    keyMenu->getNavBar()->setRightButtonText("绑定");
    addWidget(keyMenu);

    bindTypeMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    bindTypeMenu->bShowBtnTips = true; bindTypeMenu->bShowBorder = false; bindTypeMenu->bShowTitle = false;
    bindTypeMenu->bVisible = false;
    bindTypeMenu->getNavBar()->setLeftButtonText("返回");
    bindTypeMenu->getNavBar()->setRightButtonText("确定");
    bindTypeMenu->addMenuItem("1. 绑定射频数据");
    bindTypeMenu->addMenuItem("2. 绑定红外数据");
    bindTypeMenu->addMenuItem("3. 清除绑定");
    addWidget(bindTypeMenu);

    slotMenu = new UIMenu(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 5);
    slotMenu->bShowBtnTips = true; slotMenu->bShowBorder = false; slotMenu->bShowTitle = false;
    slotMenu->bVisible = false;
    slotMenu->getNavBar()->setLeftButtonText("返回");
    slotMenu->getNavBar()->setRightButtonText("确定");
    addWidget(slotMenu);

    rebuildKeyMenu();
}

HomeKeymapEditPage::~HomeKeymapEditPage() {}

void HomeKeymapEditPage::getValidKeys(int* out, int& outCount) const {
    outCount = 0;
    int n = HomeLayoutToKeyCount(profile.layout);
    for (int k = 1; k <= n; k++) out[outCount++] = k;
}

const char* HomeKeymapEditPage::keyHint(int physKey) const {
    if (profile.layout == HOME_LAYOUT_KEY1 && physKey == 1) return "单键";
    return "";
}

int HomeKeymapEditPage::menuIndexToSlotFiltered(int idx, SignalKind kind) const {
    if (idx == 0) return 0;
    if (idx >= 1 && idx <= 100) return KindIndexToSlot(kind, idx);
    return 0;
}
int HomeKeymapEditPage::slotToMenuIndexFiltered(int slot, SignalKind kind) const {
    if (slot == 0) return 0;
    if (kind == SIG_IR && SlotIsIR(slot)) return SlotToIndex(slot);
    if (kind == SIG_RF && SlotIsRF(slot)) return slot;
    // 旧编码下 slot 1..100 可能既是RF也可能是IR映射前的值
    if (kind == SIG_RF && slot >= 1 && slot <= RF_SLOT_COUNT) return slot;
    return 0;
}

// 兼容旧扁平列表（不再使用，仅保留符号）
int HomeKeymapEditPage::menuIndexToSlot(int idx) const {
    if (idx == 0) return 0;
    if (idx >= 1 && idx <= RF_SLOT_COUNT) return idx;
    if (idx > RF_SLOT_COUNT && idx <= RF_SLOT_COUNT + IR_SLOT_COUNT) return (idx - RF_SLOT_COUNT) + SLOT_IR_OFFSET;
    return 0;
}
int HomeKeymapEditPage::slotToMenuIndex(int slot) const {
    if (slot == 0) return 0;
    if (SlotIsIR(slot)) return RF_SLOT_COUNT + SlotToIndex(slot);
    if (SlotIsRF(slot)) return slot;
    return 0;
}

void HomeKeymapEditPage::rebuildKeyMenu() {
    keyMenu->clear();
    int valid[9]; int vc=0; getValidKeys(valid, vc);
    for (int i = 0; i < vc; i++) {
        int pk = valid[i];
        int slot = profile.slotMap[pk-1];
        String label = "键" + String(pk);
        const char* hint = keyHint(pk);
        if (hint[0]) label += "(" + String(hint) + ")";
        label += ": ";
        SignalEntry e;
        if (SlotIsIR(slot)) e = dataStore.ReadSignal(SIG_IR, SlotToIndex(slot));
        else if (SlotIsRF(slot)) e = dataStore.ReadSignal(SIG_RF, slot);
        else if (slot != 0) e = dataStore.ReadSignal(slot);
        if (slot != 0 && e.name.length() > 0) {
            label += (e.kind==SIG_IR?"[IR]":"[RF]") + e.name;
            label += " (#" + String(slot) + ")";
        } else if (slot != 0) {
            // 已绑定但槽位数据已被删除
            label += "(空槽 #" + String(slot) + ")";
        } else label += "----------";
        keyMenu->addMenuItem(label);
    }
}

void HomeKeymapEditPage::enterBindTypePick() {
    int valid[9]; int vc=0; getValidKeys(valid, vc);
    int idx = keyMenu->menuSel;
    if (idx < 0) idx = 0; if (idx >= vc) idx = 0;
    selectedPhysKey = valid[idx];
    // 预选中：若当前已绑定则高亮对应类型
    int cur = profile.slotMap[selectedPhysKey-1];
    if (SlotIsIR(cur)) bindTypeMenu->menuSel = 1;
    else if (SlotIsRF(cur)) bindTypeMenu->menuSel = 0;
    else bindTypeMenu->menuSel = 0;
    bindTypeMenu->menuStart = 0; bindTypeMenu->menuPos = bindTypeMenu->menuSel;
    keyMenu->bVisible = false;
    bindTypeMenu->bVisible = true;
    slotMenu->bVisible = false;
    state = STATE_BIND_TYPE;
}

void HomeKeymapEditPage::enterSlotPickFiltered(SignalKind kind) {
    pendingKind = kind;
    slotMenu->clear();
    slotMenu->addMenuItem("0. 清除绑定");
    for (int i = 1; i <= 100; i++) {
        String s = String((int)i) + ". ";
        SignalEntry e = dataStore.ReadSignal(kind, i);
        if (e.name.length() > 0) s += (kind==SIG_IR?"[IR]":"[RF]") + e.name;
        else s += "----------";
        slotMenu->addMenuItem(s);
    }
    int cur = profile.slotMap[selectedPhysKey-1];
    int menuSel = slotToMenuIndexFiltered(cur, kind);
    // 若当前绑定与 pendingKind 不一致，则默认停在 0（清除/未绑定）
    if ((kind==SIG_IR && !SlotIsIR(cur)) || (kind==SIG_RF && !SlotIsRF(cur) && cur!=0)) {
        if (cur != 0) menuSel = 0;
    }
    slotMenu->menuSel = menuSel;
    slotMenu->menuStart = 0; slotMenu->menuPos = (slotMenu->menuSel - slotMenu->menuStart);
    if (slotMenu->menuPos < 0) slotMenu->menuPos = 0;
    int itemLines = slotMenu->menuLines;
    if (slotMenu->bShowTitle) itemLines--;
    if (slotMenu->bShowBtnTips) itemLines--;
    if (slotMenu->menuPos >= itemLines) {
        slotMenu->menuStart = slotMenu->menuSel - itemLines + 1;
        slotMenu->menuPos = itemLines - 1;
    }
    bindTypeMenu->bVisible = false;
    slotMenu->bVisible = true;
    state = STATE_SLOTS;
}

void HomeKeymapEditPage::applySlotAndBack(int menuIdx) {
    int slot = menuIndexToSlotFiltered(menuIdx, pendingKind);
    profile.slotMap[selectedPhysKey-1] = (int16_t)slot;
    dataStore.SaveHomeProfile(profile);
    int prevSel = keyMenu->menuSel;
    rebuildKeyMenu();
    int vc=0; int valid[9]; getValidKeys(valid, vc);
    if (prevSel >= 0 && prevSel < vc) {
        keyMenu->menuSel = prevSel;
        int itemLines = keyMenu->menuLines;
        if (keyMenu->bShowTitle) itemLines--;
        if (keyMenu->bShowBtnTips) itemLines--;
        if (prevSel < keyMenu->menuStart) keyMenu->menuStart = prevSel;
        else if (prevSel >= keyMenu->menuStart + itemLines) keyMenu->menuStart = prevSel - itemLines + 1;
        keyMenu->menuPos = prevSel - keyMenu->menuStart;
    }
    slotMenu->bVisible = false;
    bindTypeMenu->bVisible = false;
    keyMenu->bVisible = true;
    state = STATE_KEYS;
}

void HomeKeymapEditPage::showPage() {
    UIPage::showPage();
    dataStore.LoadHomeProfile(profileId, profile);
    rebuildKeyMenu();
    state = STATE_KEYS;
    keyMenu->bVisible = true;
    bindTypeMenu->bVisible = false;
    slotMenu->bVisible = false;
}

void HomeKeymapEditPage::render(U8G2* u8g2) { UIPage::render(u8g2); }

void HomeKeymapEditPage::onButtonBack(void* context) {
    if (state == STATE_SLOTS) {
        slotMenu->getNavBar()->showLeftBlink(1,80,80,[this](){
            slotMenu->bVisible = false;
            bindTypeMenu->bVisible = true;
            state = STATE_BIND_TYPE;
        });
    } else if (state == STATE_BIND_TYPE) {
        bindTypeMenu->getNavBar()->showLeftBlink(1,80,80,[this](){
            bindTypeMenu->bVisible = false;
            keyMenu->bVisible = true;
            state = STATE_KEYS;
        });
    } else {
        keyMenu->getNavBar()->showLeftBlink(1,80,80,[this](){ uiEngine.navigateBack(); });
    }
}
void HomeKeymapEditPage::onButtonEnter(void* context) {
    if (state == STATE_KEYS) {
        keyMenu->getNavBar()->showRightBlink(1,80,80,[this](){ enterBindTypePick(); });
    } else if (state == STATE_BIND_TYPE) {
        int sel = bindTypeMenu->menuSel;
        bindTypeMenu->getNavBar()->showRightBlink(1,80,80,[this,sel](){
            if (sel == 0) enterSlotPickFiltered(SIG_RF);
            else if (sel == 1) enterSlotPickFiltered(SIG_IR);
            else {
                // 清除绑定
                profile.slotMap[selectedPhysKey-1] = 0;
                dataStore.SaveHomeProfile(profile);
                int prevSel = keyMenu->menuSel;
                rebuildKeyMenu();
                int vc=0; int valid[9]; getValidKeys(valid, vc);
                if (prevSel >= 0 && prevSel < vc) {
                    keyMenu->menuSel = prevSel;
                    int itemLines = keyMenu->menuLines;
                    if (keyMenu->bShowTitle) itemLines--;
                    if (keyMenu->bShowBtnTips) itemLines--;
                    if (prevSel < keyMenu->menuStart) keyMenu->menuStart = prevSel;
                    else if (prevSel >= keyMenu->menuStart + itemLines) keyMenu->menuStart = prevSel - itemLines + 1;
                    keyMenu->menuPos = prevSel - keyMenu->menuStart;
                }
                bindTypeMenu->bVisible = false;
                keyMenu->bVisible = true;
                state = STATE_KEYS;
            }
        });
    } else {
        int idx = slotMenu->menuSel;
        slotMenu->getNavBar()->showRightBlink(1,80,80,[this,idx](){ applySlotAndBack(idx); });
    }
}
void HomeKeymapEditPage::onButtonMenu(void* context) {
    if (state == STATE_KEYS) keyMenu->moveDown();
    else if (state == STATE_BIND_TYPE) bindTypeMenu->moveDown();
    else slotMenu->moveDown();
}
void HomeKeymapEditPage::onButton2(void* context) {
    if (state == STATE_KEYS) keyMenu->moveUp();
    else if (state == STATE_BIND_TYPE) bindTypeMenu->moveUp();
    else slotMenu->moveUp();
}
void HomeKeymapEditPage::onButton5(void* context) { onButtonEnter(context); }
void HomeKeymapEditPage::onButton8(void* context) {
    if (state == STATE_KEYS) keyMenu->moveDown();
    else if (state == STATE_BIND_TYPE) bindTypeMenu->moveDown();
    else slotMenu->moveDown();
}
