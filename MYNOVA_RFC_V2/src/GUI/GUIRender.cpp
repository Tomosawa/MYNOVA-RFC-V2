/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

#include "GUIRender.h"
#include "../Bsp/IOPin.h"
#include <U8g2lib.h>
#include "UIEngine.h"
#include "UIPage.h"
#include "Widget/UITitleBar.h"
#include "Widget/UIQuickButton.h"
#include "Widget/UIMenu.h"
#include "../Pages/Home/HomePage.h"
#include "../Pages/Home/MenuPage.h"
#include "../Pages/Settings/SettingPage.h"
#include "../Pages/Rf/ReceivePage.h"
#include "../Bsp/I2CBus.h"
#include "../Drivers/ButtonHandle.h"
#include "../Services/SystemSetting.h"
#include "../Util/TaskConfig.h"
#include "../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "GUI"

// OLED屏幕配置
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, /* clock=*/PIN_OLED_SCL, /* data=*/PIN_OLED_SDA);

UIEngine uiEngine;
HomePage uiPageHome;

GUIRender::GUIRender()
{
    drawGUITaskHandle = nullptr;
    bPowerSaveRequest = false;
    bPowerSaveActive = false;
    pendingContrast = 0;
    contrastPending = false;
    pendingRfRxFreq = 0.0f;
    rfRxPending = false;
}

// 仅初始化显示屏（用于SplashScreen阶段）
void GUIRender::initDisplay()
{
    LOG_I("GUIRender: 初始化显示屏");
    u8g2.begin();
}

// 获取U8G2指针
U8G2* GUIRender::getU8G2()
{
    return &u8g2;
}

// 完整初始化（启动UIKernel）
void GUIRender::init()
{
    LOG_I("GUIRender: 启动UIKernel");
    // 如果显示屏还没初始化，先初始化
    if (!u8g2.getU8g2()) {
        u8g2.begin();
    }

    uiEngine.setCurrentPage(&uiPageHome);
    startRenderTask();
}

// 启动UIKernel任务(钉Core1: 交互/业务核, 优先级4=全工程最高应用优先级)
void GUIRender::startRenderTask()
{
    if (drawGUITaskHandle != nullptr) {
        LOG_W("GUIRender: UIKernel任务已存在");
        return;
    }

    xTaskCreatePinnedToCore(
        uiKernelTask,               // 任务函数
        TASK_UI_NAME,               // "UIKernel"
        TASK_UI_STACK,             // 8KB
        this,                       // 任务参数
        TASK_PRIO_UI,              // 优先级4
        &drawGUITaskHandle,        // 任务句柄
        TASK_CORE_APP              // Core1
    );

    LOG_I("GUIRender: UIKernel已启动 (Core1, P4)");
}

// ── 跨线程请求(单写多读volatile, 无锁) ──
void GUIRender::requestPowerSave(bool bOn)
{
    bPowerSaveRequest = bOn;
}

void GUIRender::requestContrast(int contrast)
{
    pendingContrast = contrast;
    contrastPending = true;
}

void GUIRender::requestRfReceive(float freqMHz)
{
    pendingRfRxFreq = freqMHz;
    rfRxPending = true;
}

// 消费请求(仅UIKernel): OLED的I2C操作全部收归UIKernel, 与sendBuffer天然串行
void GUIRender::applyPendingRequests()
{
    if (bPowerSaveRequest != bPowerSaveActive) {
        bPowerSaveActive = bPowerSaveRequest;
        u8g2.setPowerSave(bPowerSaveActive);
    }
    if (contrastPending) {
        contrastPending = false;
        u8g2.setContrast(pendingContrast);
    }
    if (rfRxPending) {
        rfRxPending = false;
        // 与菜单路径一致: ReceivePage构造即启动单频点守候接收
        uiEngine.navigateTo(new ReceivePage(pendingRfRxFreq));
    }
}

// 渲染一帧并I2C发送(OLED与WM8960共享总线, 发送期间持I2CBus锁)
void GUIRender::sendFrame()
{
    u8g2.clearBuffer();
    uiEngine.render(&u8g2);
    I2CBus::lock();
    u8g2.sendBuffer();
    I2CBus::unlock();
}

// ==================== UIKernel: 唯一UI任务 ====================
// 串行执行: 按键事件分发 → 请求消费 → update(动画/页面) → render → send
// 息屏时阻塞等事件(不再空转), 任意按键事件即唤醒亮屏。
void GUIRender::uiKernelTask(void* pvParameters)
{
    GUIRender* self = (GUIRender*)pvParameters;
    ButtonHandleEvent ev;
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t frameTicks = pdMS_TO_TICKS(10);  // 帧节拍上限(≈100fps, 含渲染耗时自然降频)

    while (true)
    {
        // 1) 按键事件: 亮屏时非阻塞清空队列; 息屏时阻塞等待唤醒
        TickType_t wait = self->bPowerSaveActive ? portMAX_DELAY : 0;
        while (ButtonHandle::pollEvent(ev, wait)) {
            ButtonHandle::dispatch(ev);   // 内含resetIdleTimer→息屏时自动请求亮屏
            wait = 0;                     // 后续待决事件非阻塞取
        }

        // 2) 消费跨线程请求(息屏/亮度), 息屏解除后本帧即恢复渲染
        self->applyPendingRequests();

        // 3) 息屏中: 不渲染, 回到阻塞等待
        if (self->bPowerSaveActive) {
            continue;
        }

        // 4) UI更新 + 渲染发送(同一任务串行, 页面生命周期天然安全)
        uiEngine.update();
        self->sendFrame();

        // 5) 帧节拍(事件驱动+节流: 无事件时每10ms一帧上限)
        vTaskDelayUntil(&lastWake, frameTicks);
    }
}

GUIRender::~GUIRender()
{
    if (drawGUITaskHandle)
    {
        vTaskDelete(drawGUITaskHandle);
    }
}
