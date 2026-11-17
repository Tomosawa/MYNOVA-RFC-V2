/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include <Arduino.h>

class BatteryManager {
public:
    BatteryManager();
    ~BatteryManager();

    void init();
    float readVoltage();
    float calculateBatteryPercent(float voltage);
    float getLastVoltage() const { return lastVoltage; }  // 最近一次采样电压(不触发ADC)

private:
    // 内部ADC读取方法
    uint32_t readADC();
    
    volatile float lastVoltage = 0.0;  // 最近一次采样的电池电压缓存
    
    // FreeRTOS相关成员
    TaskHandle_t batteryMonitorTaskHandle;      // 电池监测任务句柄
    
    // 电池监测任务函数
    static void batteryMonitorTask(void* pvParameters);
};

#endif