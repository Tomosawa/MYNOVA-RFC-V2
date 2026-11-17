/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// PowerManager.h —— 分级电源管理框架（规划 §七 P4.3）
// 设计:
//   · ISuspendable: 各驱动实现 suspend()/resume(), 注册到 PowerManager;
//   · suspendAll()/resumeAll(): 统一外设收尾(休眠前/唤醒后), 消除
//     "收尾逻辑硬编码在SystemSetting、漏关一路就漏电"的隐患;
//   · 息屏: 已由 UIKernel 事件驱动挂起实现(GUIRender, 无需本模块);
//   · 深度休眠: 沿用 SystemSetting.enterSleep 已实测路径,
//     本模块提供 deepSleepFinish() 作为可选的统一收尾切面;
//   · 自动 light sleep: 预留开关(需硬件实测 WiFi/唤醒行为后启用)。
#ifndef BSP_POWERMANAGER_H
#define BSP_POWERMANAGER_H

#include <Arduino.h>

// 可挂起外设接口: 驱动注册后, 休眠前统一 suspend、唤醒后统一 resume
class ISuspendable {
public:
    virtual ~ISuspendable() {}
    // 休眠前收尾(断电/进idle/停捕获), 必须可重入且幂等
    virtual void suspend() = 0;
    // 唤醒后恢复(若该外设需要在使用时才恢复, 允许空实现)
    virtual void resume() {}
};

class PowerManager {
public:
    // 注册可挂起外设(最多8个, 重复注册忽略)
    static void registerSuspendable(ISuspendable* dev);

    // 统一收尾: 按注册逆序逐个 suspend
    static void suspendAll();
    // 统一恢复: 按注册顺序逐个 resume
    static void resumeAll();

    // 自动light sleep总开关(默认关闭):
    // 开启后空闲CPU进入浅睡(保PSRAM/RAM, 按键GPIO唤醒)。
    // 注意: WiFi/AP活跃时的modem-sleep联动、唤醒延迟需硬件实测,
    // 实测通过后再置 true(规划P4遗留实测项)。
    static constexpr bool AUTO_LIGHT_SLEEP_ENABLED = false;

private:
    static constexpr int MAX_DEVS = 8;
    static ISuspendable* s_devs[MAX_DEVS];
    static int s_count;
};

#endif // BSP_POWERMANAGER_H
