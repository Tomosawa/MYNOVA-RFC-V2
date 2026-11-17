/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

#ifndef __BUTTONHANDLE_H_
#define __BUTTONHANDLE_H_
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "ButtonDetector.h"

// 按钮事件枚举
enum ButtonHandleEvent {
    BTN_BACK,
    BTN_MENU,
    BTN_ENTER,
    BTN_1,
    BTN_2,
    BTN_3,
    BTN_4,
    BTN_5,
    BTN_6,
    BTN_7,
    BTN_8,
    BTN_9,
    BTN_5_DOWN,   // 5键按下沿(PTT按住说话用, 对讲页消费)
    BTN_5_UP,      // 5键释放沿(PTT松开发送用)
    BTN_WAKE       // 串口调试: 仅唤醒UIKernel(息屏时用), 无页面动作
};

// PTT说明: BTN_5_DOWN/UP 5键沿事件由对讲语音页消费(onButton5Down/Up), 其他页基类空实现忽略。
// 为避免按住5期间LONG_PRESS重复触发导致误动作, 对讲语音页S1态内onButton5直接返回。

class ButtonHandle
{
public:
    ButtonHandle();
    ~ButtonHandle();

    // 初始化: 创建事件队列+注册12键中断(必须先于UIKernel启动调用)
    // 注意: 构造函数只登记全局指针, 不碰FreeRTOS(全局构造期安全性, 规划P0)
    void init();

    // ==================== UIKernel 专用接口 ====================
    // 轮询事件: wait=0非阻塞清空队列 / portMAX_DELAY阻塞唤醒(息屏用)
    // 返回true时 ev 已填充
    static bool pollEvent(ButtonHandleEvent& ev, TickType_t wait);

    // 分发事件到当前页面(resetIdleTimer + onButtonX 虚函数调用)
    static void dispatch(ButtonHandleEvent ev);

    // 串口调试: 向事件队列投递(任意任务可调, 与ISR同路径; 队列满则丢弃)
    static void inject(ButtonHandleEvent ev);

private:
    // 按钮中断回调(消抖定时器上下文): 只做入队
    static void Click_Handle_BtnBack();
    static void Click_Handle_BtnMenu();
    static void Click_Handle_BtnEnter();
    static void Click_Handle_Btn1();
    static void Click_Handle_Btn2();
    static void Click_Handle_Btn3();
    static void Click_Handle_Btn4();
    static void Click_Handle_Btn5();
    static void Click_Handle_Btn6();
    static void Click_Handle_Btn7();
    static void Click_Handle_Btn8();
    static void Click_Handle_Btn9();
    static void Down_Handle_Btn5();   // 5键按下沿(PTT)
    static void Up_Handle_Btn5();     // 5键释放沿(PTT)
    static void postEventISR(ButtonHandleEvent event);

private:
    // 按钮检测器实例
    ButtonDetector* pBtnBack;
    ButtonDetector* pBtnMenu;
    ButtonDetector* pBtnEnter;
    ButtonDetector* pBtn1;
    ButtonDetector* pBtn2;
    ButtonDetector* pBtn3;
    ButtonDetector* pBtn4;
    ButtonDetector* pBtn5;
    ButtonDetector* pBtn6;
    ButtonDetector* pBtn7;
    ButtonDetector* pBtn8;
    ButtonDetector* pBtn9;

    // 按钮事件队列(init时创建; ISR/poll 侧均判空)
    QueueHandle_t buttonEventQueue;
};

#endif
