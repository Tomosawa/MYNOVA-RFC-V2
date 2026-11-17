/* 
* Copyright (c) 2026 Tomosawa 
*/

#ifndef HOMEKEYMAPEDITPAGE_H
#define HOMEKEYMAPEDITPAGE_H

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIMenu.h"
#include "../../Services/DataStore.h"

class HomeKeymapEditPage : public UIPage {
public:
    explicit HomeKeymapEditPage(int profileId);
    ~HomeKeymapEditPage() override;
    void showPage() override;
    void render(U8G2* u8g2) override;
    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;
private:
    enum State { STATE_KEYS, STATE_BIND_TYPE, STATE_SLOTS };
    void rebuildKeyMenu();
    void enterBindTypePick();
    void enterSlotPickFiltered(SignalKind kind);
    void applySlotAndBack(int menuIdx); // filtered: 0=>0, 1..100=>KindIndexToSlot(pendingKind, idx)
    int menuIndexToSlotFiltered(int idx, SignalKind kind) const;
    int slotToMenuIndexFiltered(int slot, SignalKind kind) const;
    // 兼容旧扁平列表（保留供迁移期编译兼容，实际不再使用）
    int menuIndexToSlot(int idx) const;
    int slotToMenuIndex(int slot) const;
    void getValidKeys(int* out, int& outCount) const;
    const char* keyHint(int physKey) const;
    int profileId;
    HomeProfile profile;
    State state = STATE_KEYS;
    int selectedPhysKey = 1;
    SignalKind pendingKind = SIG_RF;
    UIMenu* keyMenu = nullptr;
    UIMenu* bindTypeMenu = nullptr;
    UIMenu* slotMenu = nullptr;
};

#endif
