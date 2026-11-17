/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// I2CBus.h —— 共享I2C总线锁（规划 §七 P4）
// OLED(U8g2)与WM8960共用一条硬件I2C(SCL=0/SDA=1), 发送帧/写编解码寄存器
// 期间必须持锁防并发破坏总线状态。
// 所有权归属本总线对象——修复原"锁挂在audioPlayer成员上被GUIRender跨层引用"
// 的倒置依赖; 两个驱动均向本对象取锁。
#ifndef BSP_I2CBUS_H
#define BSP_I2CBUS_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class I2CBus {
public:
    // 创建总线锁(幂等, 开机调用一次); 返回锁句柄供兼容代码直接使用
    static SemaphoreHandle_t begin();
    // 锁句柄(null=尚未begin)
    static SemaphoreHandle_t mutex() { return s_mutex; }
    // 便捷加锁/解锁(lock判null空安全)
    static bool lock(TickType_t timeout = portMAX_DELAY);
    static void unlock();

private:
    static SemaphoreHandle_t s_mutex;
};

#endif // BSP_I2CBUS_H
