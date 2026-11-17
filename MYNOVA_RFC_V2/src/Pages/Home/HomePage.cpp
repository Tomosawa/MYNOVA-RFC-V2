/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

// HomePage.cpp — 1..9键布局引擎（9档），按 HPROF 渲染
#include "HomePage.h"
#include "../../GUI/UIEngine.h"
#include "MenuPage.h"
#include "../../Drivers/RadioHelper.h"
#include "../../Services/DataStore.h"
#include "../../Services/SystemSetting.h"
#include "../../Services/WiFiManager.h"
#include "../../Drivers/BatteryManager.h"
#include <WiFi.h>

extern UIEngine uiEngine;
extern RadioHelper radioHelper;
extern IRHelper irHelper;
extern DataStore dataStore;
extern SystemSetting systemSetting;
extern WiFiManager wifiManager;
extern BatteryManager batteryManager;

struct HomeBtnGeom { bool vis; int x,y,w,h; };

// 1..9 键：物理键 1..N 按视觉顺序依次铺满（行优先）
// 可视区约 y=10..63（标题10px），按钮高度在 54px 内分配
static const HomeBtnGeom LAYOUT_GEOMS[9][9] = {
    // 1键：全屏大键
    {
        {1,  1,11,126,52},{0, 0,0,0,0},{0, 0,0,0,0},
        {0,  0,0,0,0},{0, 0,0,0,0},{0, 0,0,0,0},
        {0,  0,0,0,0},{0, 0,0,0,0},{0, 0,0,0,0}
    },
    // 2键：左右两半
    {
        {1,  1,11,62,52},{1, 65,11,62,52},{0, 0,0,0,0},
        {0,  0,0,0,0},{0, 0,0,0,0},{0, 0,0,0,0},
        {0,  0,0,0,0},{0, 0,0,0,0},{0, 0,0,0,0}
    },
    // 3键：上1通栏 + 下2
    {
        {1,  1,10,126,16},{1,  1,28,62,25},{1, 65,28,62,25},
        {0,  0,0,0,0},{0, 0,0,0,0},{0, 0,0,0,0},
        {0,  0,0,0,0},{0, 0,0,0,0},{0, 0,0,0,0}
    },
    // 4键：2x2（前4个连续）
    {
        {1,  1,11,62,25},{1, 65,11,62,25},{1,  1,38,62,25},
        {1, 65,38,62,25},{0,  0, 0, 0, 0},{0,  0, 0, 0, 0},
        {0,  0, 0, 0, 0},{0,  0, 0, 0, 0},{0,  0, 0, 0, 0}
    },
    // 5键：上1通栏 + 下2x2（前5个连续）
    {
        {1,  1,10,126,16},{1,  1,28,62,17},{1, 65,28,62,17},
        {1,  1,47,62,16},{1, 65,47,62,16},{0,  0, 0, 0, 0},
        {0,  0, 0, 0, 0},{0,  0, 0, 0, 0},{0,  0, 0, 0, 0}
    },
    // 6键：2x3（前6个连续）
    {
        {1,  1,11,62,16},{1, 65,11,62,16},{1,  1,29,62,16},
        {1, 65,29,62,16},{1,  1,47,62,16},{1, 65,47,62,16},
        {0,  0, 0, 0, 0},{0,  0, 0, 0, 0},{0,  0, 0, 0, 0}
    },
    // 7键：上1通栏 + 下2x3（前7个连续）
    {
        {1,  1,10,126,12},{1,  1,24,62,12},{1, 65,24,62,12},
        {1,  1,38,62,12},{1, 65,38,62,12},{1,  1,52,62,11},
        {1, 65,52,62,11},{0,  0, 0, 0, 0},{0,  0, 0, 0, 0}
    },
    // 8键：2x4（前8个连续）
    {
        {1,  1,11,62,12},{1, 65,11,62,12},{1,  1,25,62,12},
        {1, 65,25,62,12},{1,  1,39,62,12},{1, 65,39,62,12},
        {1,  1,53,62,10},{1, 65,53,62,10},{0,  0, 0, 0, 0}
    },
    // 9键：3x3
    {
        {1,  0,10,42,17},{1, 43,10,42,17},{1, 86,10,42,17},
        {1,  0,28,42,17},{1, 43,28,42,17},{1, 86,28,42,17},
        {1,  0,46,42,17},{1, 43,46,42,17},{1, 86,46,42,17}
    }
};

HomePage::HomePage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT) {
    // quickButtons/titleBar 为本页成员对象(随全局对象存续), 页面只借引用不负责释放
    for (int i = 0; i < 9; i++) addWidget(&quickButtons[i], false);
    addWidget(&titleBar, false);
    titleBar.x = 0; titleBar.y = 0;
    titleBar.width = SCREEN_WIDTH; titleBar.height = 10;
    titleBar.showSendAnime();
    applyProfile();
    // 全局构造期不碰wifiManager/WiFi/systemSetting:
    // 跨编译单元构造顺序未定义, 查询可能崩溃; 先给安全默认值,
    // showPage()/update()(setup之后运行)再按真实状态刷新
    titleBar.setSoundState(1);
    titleBar.setWifiIcon(false, false);
    titleBar.setAPIcon(false, false);
}

void HomePage::showPage() {
    configRevision = dataStore.GetRemoteConfigRevision();
    activeHomeRevision = dataStore.GetActiveHomeRevision();
    applyProfile();
    updateStatus();
}

bool HomePage::isLayoutKeyValid(int physKey) const {
    if (physKey < 1 || physKey > 9) return false;
    if (profile.layout == HOME_LAYOUT_KEY1) return true; // 单键：任意物理键触发键1
    int n = HomeLayoutToKeyCount(profile.layout);
    return physKey >= 1 && physKey <= n;
}

void HomePage::applyProfile() {
    activeProfileId = dataStore.GetActiveHomeProfile();
    if (!dataStore.LoadHomeProfile(activeProfileId, profile)) {
        auto list = dataStore.ListHomeProfiles();
        if (!list.empty()) { profile = list[0]; activeProfileId = profile.id; }
        else { profile = HomeProfile(); profile.id = 0; profile.layout = HOME_LAYOUT_KEY9; profile.name = "主页1"; activeProfileId = 0; }
    }
    if (profile.layout > HOME_LAYOUT_KEY9) profile.layout = HOME_LAYOUT_KEY9;

    const HomeBtnGeom* geoms = LAYOUT_GEOMS[profile.layout];
    bool largeLabel = (profile.layout != HOME_LAYOUT_KEY9);

    for (int i = 0; i < 9; i++) {
        cachedSignalData[i] = SignalEntry();
        cachedSlotIndex[i] = 0;
        int slot = profile.slotMap[i];
        if (slot == 0) {
            const HomeBtnGeom& g = geoms[i];
            if (profile.layout == HOME_LAYOUT_KEY1 && i == 0) {
                quickButtons[i].bVisible = true;
                quickButtons[i].x = geoms[0].x; quickButtons[i].y = geoms[0].y;
                quickButtons[i].width = geoms[0].w; quickButtons[i].height = geoms[0].h;
                quickButtons[i].keyNum = "";
                quickButtons[i].labelFont = largeLabel ? u8g2_font_wqy12_t_gb2312 : nullptr;
                quickButtons[i].label = profile.name.length() > 0 ? profile.name : "按任意键发送";
            } else {
                quickButtons[i].bVisible = g.vis;
                quickButtons[i].x = g.x; quickButtons[i].y = g.y;
                quickButtons[i].width = g.w; quickButtons[i].height = g.h;
                quickButtons[i].keyNum = String(i + 1);
                quickButtons[i].labelFont = largeLabel ? u8g2_font_wqy12_t_gb2312 : nullptr;
                if (g.vis) quickButtons[i].label = "----------"; else quickButtons[i].label = "";
            }
            quickButtons[i].roundCorner = (profile.layout == HOME_LAYOUT_KEY1) ? 4 : 2;
            continue;
        }
        SignalEntry e;
        if (SlotIsIR(slot)) e = dataStore.ReadSignal(SIG_IR, SlotToIndex(slot));
        else if (SlotIsRF(slot)) e = dataStore.ReadSignal(SIG_RF, slot);
        else {
            e = dataStore.ReadSignal(slot);
            if (e.name.length() == 0) {
                const HomeBtnGeom& g = geoms[i];
                quickButtons[i].bVisible = g.vis;
                quickButtons[i].x = g.x; quickButtons[i].y = g.y;
                quickButtons[i].width = g.w; quickButtons[i].height = g.h;
                quickButtons[i].keyNum = String(i + 1);
                quickButtons[i].labelFont = largeLabel ? u8g2_font_wqy12_t_gb2312 : nullptr;
                if (g.vis) quickButtons[i].label = "----------"; else quickButtons[i].label = "";
                quickButtons[i].roundCorner = (profile.layout == HOME_LAYOUT_KEY1) ? 4 : 2;
                continue;
            }
        }
        if (e.name.length() == 0) {
            const HomeBtnGeom& g = geoms[i];
            quickButtons[i].bVisible = g.vis;
            quickButtons[i].x = g.x; quickButtons[i].y = g.y;
            quickButtons[i].width = g.w; quickButtons[i].height = g.h;
            quickButtons[i].keyNum = String(i + 1);
            quickButtons[i].labelFont = largeLabel ? u8g2_font_wqy12_t_gb2312 : nullptr;
            if (g.vis) quickButtons[i].label = "----------"; else quickButtons[i].label = "";
            quickButtons[i].roundCorner = (profile.layout == HOME_LAYOUT_KEY1) ? 4 : 2;
            continue;
        }
        cachedSignalData[i] = e;
        cachedSlotIndex[i] = slot;

        if (profile.layout == HOME_LAYOUT_KEY1 && i == 0) {
            quickButtons[i].bVisible = true;
            quickButtons[i].x = geoms[0].x; quickButtons[i].y = geoms[0].y;
            quickButtons[i].width = geoms[0].w; quickButtons[i].height = geoms[0].h;
            quickButtons[i].keyNum = "";
            quickButtons[i].labelFont = largeLabel ? u8g2_font_wqy12_t_gb2312 : nullptr;
            String pfx = (cachedSignalData[0].kind == SIG_IR) ? "[IR]" : "[RF]";
            quickButtons[i].label = pfx + cachedSignalData[0].name;
        } else {
            const HomeBtnGeom& g = geoms[i];
            quickButtons[i].bVisible = g.vis;
            quickButtons[i].x = g.x; quickButtons[i].y = g.y;
            quickButtons[i].width = g.w; quickButtons[i].height = g.h;
            quickButtons[i].keyNum = String(i + 1);
            quickButtons[i].labelFont = largeLabel ? u8g2_font_wqy12_t_gb2312 : nullptr;
            if (!g.vis) { quickButtons[i].label = ""; }
            else {
                String pfx = (cachedSignalData[i].kind == SIG_IR) ? "[IR]" : "[RF]";
                quickButtons[i].label = pfx + cachedSignalData[i].name;
            }
        }
        quickButtons[i].roundCorner = (profile.layout == HOME_LAYOUT_KEY1) ? 4 : 2;
    }
}

void HomePage::updateStatus() {
    titleBar.setSoundState(systemSetting.getSoundAlertEnable() ? 1 : 0);
    SystemConfig cfg = systemSetting.getConfig();
    // 未连接只闪烁: enabled=开关已开, connected=实际连上/就绪
    bool wifiConn = (WiFi.status() == WL_CONNECTED);
    bool apReady = wifiManager.isAPStarted();
    titleBar.setWifiIcon(cfg.WifiEnabled, wifiConn);
    titleBar.setAPIcon(cfg.APEnabled, apReady);
    // 同步轮询缓存, 避免update()误判重复刷新
    lastWifiConn = wifiConn;
    lastAPReady = apReady;
    lastWifiEnabled = cfg.WifiEnabled;
    lastAPEnabled = cfg.APEnabled;
    lastIconPollMs = millis();
}

void HomePage::update() {
    // 连接状态变化才刷新图标: polling 500ms, 状态翻转或开关翻转时 updateStatus
    unsigned long now = millis();
    if (now - lastIconPollMs < 500) return;
    lastIconPollMs = now;

    // Web 和 Home Assistant 从其他任务切换主页，界面在 UI 任务里重新加载。
    uint32_t revision = dataStore.GetRemoteConfigRevision();
    uint32_t homeRevision = dataStore.GetActiveHomeRevision();
    if (revision != configRevision || homeRevision != activeHomeRevision) {
        configRevision = revision;
        activeHomeRevision = homeRevision;
        applyProfile();
    }

    // 电池电压: 轮询BatteryManager快照(volatile), 在UIKernel上下文刷新等级显示
    // (取代原Battery任务跨线程直调setBattery, 单线程化改造)
    setBattery(batteryManager.getLastVoltage());

    SystemConfig cfg = systemSetting.getConfig();
    bool wifiConn = (WiFi.status() == WL_CONNECTED);
    bool apReady = wifiManager.isAPStarted();
    if (wifiConn != lastWifiConn || apReady != lastAPReady ||
        cfg.WifiEnabled != lastWifiEnabled || cfg.APEnabled != lastAPEnabled) {
        lastWifiConn = wifiConn;
        lastAPReady = apReady;
        lastWifiEnabled = cfg.WifiEnabled;
        lastAPEnabled = cfg.APEnabled;
        updateStatus();
    }
}

void HomePage::handlePhysKey(int physKey) {
    if (!isLayoutKeyValid(physKey)) return;
    int btnIndex = physKey - 1;
    if (profile.layout == HOME_LAYOUT_KEY1) btnIndex = 0;
    if (btnIndex < 0 || btnIndex >= 9) return;
    if (cachedSlotIndex[btnIndex] == 0 || cachedSignalData[btnIndex].name.length() == 0) return;
    unsigned long now = millis();
    if (now - lastSendTime < SEND_DEBOUNCE_MS) return;
    lastSendTime = now;
    if (cachedSignalData[btnIndex].kind == SIG_RF) {
        radioHelper.SendData(cachedSignalData[btnIndex].rf);
        titleBar.showSendAnime();
    }
    else {
        int irIdx = SlotIsIR(cachedSlotIndex[btnIndex]) ? SlotToIndex(cachedSlotIndex[btnIndex]) : cachedSlotIndex[btnIndex];
        irHelper.SendFromSlot(irIdx);
        titleBar.showIRSendAnime();
    }
    int flashIndex = (profile.layout == HOME_LAYOUT_KEY1) ? 0 : btnIndex;
    if (quickButtons[flashIndex].bVisible) quickButtons[flashIndex].showBlink(3, 100, 100);
}

void HomePage::onButtonMenu(void* context) { uiEngine.navigateTo(new MenuPage(), ANIME_SLIDE_IN_UP); }
void HomePage::onButtonEnter(void* context) {
    if (profile.layout == HOME_LAYOUT_KEY1) handlePhysKey(1);
}
void HomePage::onButton1(void* context) { handlePhysKey(1); }
void HomePage::onButton2(void* context) { handlePhysKey(2); }
void HomePage::onButton3(void* context) { handlePhysKey(3); }
void HomePage::onButton4(void* context) { handlePhysKey(4); }
void HomePage::onButton5(void* context) { handlePhysKey(5); }
void HomePage::onButton6(void* context) { handlePhysKey(6); }
void HomePage::onButton7(void* context) { handlePhysKey(7); }
void HomePage::onButton8(void* context) { handlePhysKey(8); }
void HomePage::onButton9(void* context) { handlePhysKey(9); }

void HomePage::setBattery(float voltage) {
    if (voltage >= 4.0) titleBar.batteryLevel = 5;
    else if (voltage >= 3.8) titleBar.batteryLevel = 4;
    else if (voltage >= 3.6) titleBar.batteryLevel = 3;
    else if (voltage >= 3.4) titleBar.batteryLevel = 2;
    else if (voltage >= 3.2) titleBar.batteryLevel = 1;
    else titleBar.batteryLevel = 0;
}
