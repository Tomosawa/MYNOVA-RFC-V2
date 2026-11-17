/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

#include "BatteryManager.h"
#include "../Bsp/IOPin.h"
#include "../Util/TaskConfig.h"
#include "../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "BATT"

// ── 电池电压换算说明(单位链: ADC原始值 → mV → 分压还原 → V) ──
// 分压网络: VBAT ──[R1=300k]──●──[R2=100k]──GND, ADC采集点在●
//   ADC读数(mV) = VBAT × R2/(R1+R2) = VBAT × 1/4
//   还原: VBAT(mV) = ADC(mV) × (R1+R2)/R2 = ADC(mV) × 4
//   offsetVoltage=100mV 为实测偏置补偿(标定常量), 加在还原后的mV上
// 最终: readVoltage() 返回 V (浮点), lastVoltage 缓存最近一次结果
static const float resistanceLow = 100000.0;  // (Ω) R2(下分压, 接地侧)
static const float resistanceHigh = 300000.0; // (Ω) R1(上分压, 接VBAT侧)
static const float offsetMilliVolt = 100.0f;  // (mV) 实测偏置补偿标定值

// ADC 采样次数（用于平均滤波）
#define ADC_SAMPLES 20

BatteryManager::BatteryManager():
batteryMonitorTaskHandle(nullptr)
{
}

BatteryManager::~BatteryManager() {
    if (batteryMonitorTaskHandle) {
        vTaskDelete(batteryMonitorTaskHandle);
    }
}

void BatteryManager::init() {
    LOG_I("BatteryManager init");
    pinMode(PIN_BATTERY, INPUT);

    analogSetAttenuation(ADC_11db);  // 设置衰减，支持 0-3.3V 输入范围
    analogReadResolution(12);         // 12位分辨率 (0-4095)

    // 电池监测任务(慢周期后台服务: 钉系统核Core0, 优先级1, 规划§4.3)
    xTaskCreatePinnedToCore(
        batteryMonitorTask,
        TASK_BATT_NAME,
        TASK_BATT_STACK,
        this,
        TASK_PRIO_BG,
        &batteryMonitorTaskHandle,
        TASK_CORE_SYSTEM
    );
    LOG_I("BatteryMonitorTask started (Core0)");
}

// 多次采样取平均, 返回ADC引脚毫伏值(分压后)
uint32_t BatteryManager::readADC()
{
    uint32_t sum = 0;

    // 多次采样取平均值，减少噪声
    for (int i = 0; i < ADC_SAMPLES; i++) {
        sum += analogReadMilliVolts(PIN_BATTERY);
        vTaskDelay(pdMS_TO_TICKS(1));  // 短暂延迟，让 ADC 稳定
    }

    return sum / ADC_SAMPLES;  // 单位(mV, 分压点电压)
}

// 通过分压比还原电池电压, 返回 V
float BatteryManager::readVoltage() {
    uint32_t mvAtDivider = readADC();                       // 分压点电压(mV)
    float batteryMilliVolt = (resistanceHigh + resistanceLow) / resistanceLow
                             * (float)mvAtDivider
                             + offsetMilliVolt;             // 还原+偏置(mV)
    LOG_I("Battery: %d mV", (int)batteryMilliVolt);
    return batteryMilliVolt / 1000.0f;                      // mV → V
}

float BatteryManager::calculateBatteryPercent(float voltage) {
    const float battery_min = 3.0;  // (V) 关机电压
    const float battery_max = 4.2;  // (V) 满充电压
    float percent = (voltage - battery_min) / (battery_max - battery_min) * 100.0;
    if (percent < 0)
        percent = 0;
    else if (percent > 100.0)
        percent = 100.0;

    return percent;
}

// 电池监测任务: 周期采样写入 lastVoltage 快照(不含UI操作;
// 电压等级由 HomePage.update() 在UIKernel上下文轮询快照刷新, 规划§4.6)
void BatteryManager::batteryMonitorTask(void* pvParameters)
{
    BatteryManager* batteryManager = static_cast<BatteryManager*>(pvParameters);

    while (true) {
        float voltage = batteryManager->readVoltage();
        batteryManager->lastVoltage = voltage;   // volatile快照, UIKernel轮询读取

        // 每10秒执行一次
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
