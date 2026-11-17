/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

// HomePage.h — 多主页引擎（全局单例语义不变，内部按 HPROF 渲染）
#ifndef HomePage_h
#define HomePage_h

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UITitleBar.h"
#include "../../GUI/Widget/UIQuickButton.h"
#include "../../GUI/GUIRender.h"
#include "../../Services/DataStore.h"
#include "../../Drivers/RadioHelper.h"
#include "../../Drivers/IRHelper.h"

class HomePage : public UIPage {
public:
    HomePage();

    void onButtonMenu(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButton1(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton3(void* context = nullptr) override;
    void onButton4(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton6(void* context = nullptr) override;
    void onButton7(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;
    void onButton9(void* context = nullptr) override;

    void showPage() override;
    void setBattery(float voltage);
    void updateStatus();
    void update() override;

private:
    void applyProfile();
    bool isLayoutKeyValid(int physKey) const; // physKey 1..9
    void handlePhysKey(int physKey);          // physKey 1..9

    UITitleBar titleBar;
    UIQuickButton quickButtons[9];
    HomeProfile profile;
    int activeProfileId = 0;
    uint32_t configRevision = 0;
    uint32_t activeHomeRevision = 0;
    SignalEntry cachedSignalData[9];
    int cachedSlotIndex[9];
    unsigned long lastSendTime = 0;
    static const unsigned long SEND_DEBOUNCE_MS = 200;
    // 顶部图标轮询缓存(避免每帧读WiFi状态)
    unsigned long lastIconPollMs = 0;
    bool lastWifiConn = false;
    bool lastAPReady = false;
    bool lastWifiEnabled = false;
    bool lastAPEnabled = false;
};

#endif
