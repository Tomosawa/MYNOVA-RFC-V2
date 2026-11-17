/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// GUIRender.h —— UIKernel: 全工程唯一的UI任务(规划 §七 P3)
// 职责(单线程串行): 按键事件轮询/分发 → 跨线程请求消费 → 动画/页面update
//                   → render → I2C锁内sendBuffer。
// 所有页面/UIEngine操作只允许本任务执行, 消除原渲染任务与按键任务的竞态。
// 息屏: requestPowerSave(true)后本任务阻塞等按键事件唤醒, 不再空转刷屏。
#ifndef __GUIRENDER_H_
#define __GUIRENDER_H_
#include <Arduino.h>
#include <U8g2lib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "UIPage.h"

//数据最大数量
#define DATA_LINE_MAX       100

#define SCREEN_WIDTH        128
#define SCREEN_HEIGHT       64

class GUIRender
{
public:
    GUIRender();
    ~GUIRender();
    void init();                 // 完整初始化(启动UIKernel)
    void initDisplay();         // 仅初始化显示屏（用于SplashScreen）
    void startRenderTask();     // 启动UIKernel任务
    U8G2* getU8G2();            // 获取U8G2指针(仅UIKernel上下文使用)

    // ── 跨线程请求接口(任意任务可调, UIKernel帧内消费执行) ──
    // 息屏/亮屏请求(取代原setPowerSave): OLED的I2C操作收归UIKernel串行执行
    void requestPowerSave(bool bOn);
    bool isPowerSaveActive() const { return bPowerSaveActive; }
    // 亮度请求(取代原setContrast): 0-255对比度
    void requestContrast(int contrast);
    // 串口调试请求: 直接进入固定频点射频接收页(如 "RF:RX:433.92")
    // 任意任务可调, UIKernel下一帧消费执行(与菜单"射频接收→固定频点"同一页面)
    void requestRfReceive(float freqMHz);

    TaskHandle_t drawGUITaskHandle;   // UIKernel任务句柄
private:
    static void uiKernelTask(void* pvParameters);
    void applyPendingRequests();      // 消费跨线程请求(仅UIKernel调用)
    void sendFrame();                 // 渲染+I2C锁内发送(仅UIKernel调用)

    // 跨线程请求状态(volatile单写多读, UIKernel消费)
    volatile bool bPowerSaveRequest;
    bool bPowerSaveActive;            // 实际生效状态(仅UIKernel改写)
    volatile int pendingContrast;
    volatile bool contrastPending;
    volatile float pendingRfRxFreq;
    volatile bool rfRxPending;
};

#endif
