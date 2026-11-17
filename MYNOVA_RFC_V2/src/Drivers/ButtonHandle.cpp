/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

#include "ButtonHandle.h"
#include "../Bsp/IOPin.h"
#include "../GUI/UIPage.h"
#include "../GUI/UIEngine.h"
#include "ButtonDetector.h"
#include "../Services/SystemSetting.h"
#include "../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "BTN"

extern UIEngine uiEngine;
extern SystemSetting systemSetting;

// 全局ButtonHandle实例指针(构造期仅赋指针, 不调FreeRTOS)
ButtonHandle* g_buttonHandle = nullptr;

ButtonHandle::ButtonHandle()
{
    g_buttonHandle = this;
    buttonEventQueue = nullptr;   // 队列延迟到init()创建(全局构造期禁用FreeRTOS API)
    pBtnBack = pBtnMenu = pBtnEnter = nullptr;
    pBtn1 = pBtn2 = pBtn3 = pBtn4 = pBtn5 = nullptr;
    pBtn6 = pBtn7 = pBtn8 = pBtn9 = nullptr;
}

void ButtonHandle::init()
{
    // 先创建队列, 再注册中断(顺序保证ISR不会投递到NULL队列)
    buttonEventQueue = xQueueCreate(10, sizeof(ButtonHandleEvent));

    // GPIO初始化（配置为输入上拉）
    pinMode(PIN_KEY_BACK, INPUT_PULLUP);
    pinMode(PIN_KEY_MENU, INPUT_PULLUP);
    pinMode(PIN_KEY_ENTER, INPUT_PULLUP);
    pinMode(PIN_KEY_1, INPUT_PULLUP);
    pinMode(PIN_KEY_2, INPUT_PULLUP);
    pinMode(PIN_KEY_3, INPUT_PULLUP);
    pinMode(PIN_KEY_4, INPUT_PULLUP);
    pinMode(PIN_KEY_5, INPUT_PULLUP);
    pinMode(PIN_KEY_6, INPUT_PULLUP);
    pinMode(PIN_KEY_7, INPUT_PULLUP);
    pinMode(PIN_KEY_8, INPUT_PULLUP);
    pinMode(PIN_KEY_9, INPUT_PULLUP);

    // 创建按钮检测器并注册中断
    // active_level = LOW 表示按下时为低电平; 长按也触发同样动作
    pBtnBack = new ButtonDetector(PIN_KEY_BACK, LOW);
    pBtnBack->attach(ButtonEvent::SINGLE_CLICK, Click_Handle_BtnBack);
    pBtnBack->start();

    pBtnMenu = new ButtonDetector(PIN_KEY_MENU, LOW);
    pBtnMenu->attach(ButtonEvent::SINGLE_CLICK, Click_Handle_BtnMenu);
    pBtnMenu->start();

    pBtnEnter = new ButtonDetector(PIN_KEY_ENTER, LOW);
    pBtnEnter->attach(ButtonEvent::SINGLE_CLICK, Click_Handle_BtnEnter);
    pBtnEnter->start();

    pBtn1 = new ButtonDetector(PIN_KEY_1, LOW);
    pBtn1->attach(ButtonEvent::SINGLE_CLICK, Click_Handle_Btn1);
    pBtn1->attach(ButtonEvent::LONG_PRESS, Click_Handle_Btn1);
    pBtn1->start();

    pBtn2 = new ButtonDetector(PIN_KEY_2, LOW);
    pBtn2->attach(ButtonEvent::SINGLE_CLICK, Click_Handle_Btn2);
    pBtn2->attach(ButtonEvent::LONG_PRESS, Click_Handle_Btn2);
    pBtn2->start();

    pBtn3 = new ButtonDetector(PIN_KEY_3, LOW);
    pBtn3->attach(ButtonEvent::SINGLE_CLICK, Click_Handle_Btn3);
    pBtn3->attach(ButtonEvent::LONG_PRESS, Click_Handle_Btn3);
    pBtn3->start();

    pBtn4 = new ButtonDetector(PIN_KEY_4, LOW);
    pBtn4->attach(ButtonEvent::SINGLE_CLICK, Click_Handle_Btn4);
    pBtn4->attach(ButtonEvent::LONG_PRESS, Click_Handle_Btn4);
    pBtn4->start();

    pBtn5 = new ButtonDetector(PIN_KEY_5, LOW);
    pBtn5->attach(ButtonEvent::SINGLE_CLICK, Click_Handle_Btn5);
    pBtn5->attach(ButtonEvent::LONG_PRESS, Click_Handle_Btn5);
    pBtn5->attach(ButtonEvent::PRESS_DOWN, Down_Handle_Btn5);
    pBtn5->attach(ButtonEvent::PRESS_UP, Up_Handle_Btn5);
    pBtn5->start();

    pBtn6 = new ButtonDetector(PIN_KEY_6, LOW);
    pBtn6->attach(ButtonEvent::SINGLE_CLICK, Click_Handle_Btn6);
    pBtn6->attach(ButtonEvent::LONG_PRESS, Click_Handle_Btn6);
    pBtn6->start();

    pBtn7 = new ButtonDetector(PIN_KEY_7, LOW);
    pBtn7->attach(ButtonEvent::SINGLE_CLICK, Click_Handle_Btn7);
    pBtn7->attach(ButtonEvent::LONG_PRESS, Click_Handle_Btn7);
    pBtn7->start();

    pBtn8 = new ButtonDetector(PIN_KEY_8, LOW);
    pBtn8->attach(ButtonEvent::SINGLE_CLICK, Click_Handle_Btn8);
    pBtn8->attach(ButtonEvent::LONG_PRESS, Click_Handle_Btn8);
    pBtn8->start();

    pBtn9 = new ButtonDetector(PIN_KEY_9, LOW);
    pBtn9->attach(ButtonEvent::SINGLE_CLICK, Click_Handle_Btn9);
    pBtn9->attach(ButtonEvent::LONG_PRESS, Click_Handle_Btn9);
    pBtn9->start();

    LOG_I("ButtonHandle init done (GPIO Interrupt Mode, UIKernel poll)");
}

// ==================== 按钮事件入链(消抖定时器上下文) ====================

void ButtonHandle::postEventISR(ButtonHandleEvent event) {
    if (g_buttonHandle && g_buttonHandle->buttonEventQueue) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(g_buttonHandle->buttonEventQueue, &event, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

void ButtonHandle::Click_Handle_BtnBack() { postEventISR(BTN_BACK); }
void ButtonHandle::Click_Handle_BtnMenu() { postEventISR(BTN_MENU); }
void ButtonHandle::Click_Handle_BtnEnter() { postEventISR(BTN_ENTER); }
void ButtonHandle::Click_Handle_Btn1() { postEventISR(BTN_1); }
void ButtonHandle::Click_Handle_Btn2() { postEventISR(BTN_2); }
void ButtonHandle::Click_Handle_Btn3() { postEventISR(BTN_3); }
void ButtonHandle::Click_Handle_Btn4() { postEventISR(BTN_4); }
void ButtonHandle::Click_Handle_Btn5() { postEventISR(BTN_5); }
void ButtonHandle::Click_Handle_Btn6() { postEventISR(BTN_6); }
void ButtonHandle::Click_Handle_Btn7() { postEventISR(BTN_7); }
void ButtonHandle::Click_Handle_Btn8() { postEventISR(BTN_8); }
void ButtonHandle::Click_Handle_Btn9() { postEventISR(BTN_9); }
void ButtonHandle::Down_Handle_Btn5()  { postEventISR(BTN_5_DOWN); }
void ButtonHandle::Up_Handle_Btn5()    { postEventISR(BTN_5_UP); }

// ==================== UIKernel 专用: 轮询与分发 ====================

bool ButtonHandle::pollEvent(ButtonHandleEvent& ev, TickType_t wait) {
    if (g_buttonHandle == nullptr || g_buttonHandle->buttonEventQueue == nullptr) {
        vTaskDelay(pdMS_TO_TICKS(10));  // 队列未就绪时的保护性等待
        return false;
    }
    return xQueueReceive(g_buttonHandle->buttonEventQueue, &ev, wait) == pdTRUE;
}

void ButtonHandle::inject(ButtonHandleEvent ev) {
    if (g_buttonHandle && g_buttonHandle->buttonEventQueue) {
        xQueueSend(g_buttonHandle->buttonEventQueue, &ev, 0);
    }
}

void ButtonHandle::dispatch(ButtonHandleEvent ev) {
    // 任何按键都重置空闲计时器(自动休眠/息屏计时基准)
    systemSetting.resetIdleTimer();

    UIPage* currentPage = uiEngine.getCurrentPage();
    if (currentPage == nullptr) {
        return;
    }
    switch (ev) {
        case BTN_BACK:     currentPage->onButtonBack();   break;
        case BTN_MENU:     currentPage->onButtonMenu();   break;
        case BTN_ENTER:    currentPage->onButtonEnter();  break;
        case BTN_1:        currentPage->onButton1();      break;
        case BTN_2:        currentPage->onButton2();      break;
        case BTN_3:        currentPage->onButton3();      break;
        case BTN_4:        currentPage->onButton4();      break;
        case BTN_5:        currentPage->onButton5();      break;
        case BTN_6:        currentPage->onButton6();      break;
        case BTN_7:        currentPage->onButton7();      break;
        case BTN_8:        currentPage->onButton8();      break;
        case BTN_9:        currentPage->onButton9();      break;
        case BTN_5_DOWN:   currentPage->onButton5Down();  break;
        case BTN_5_UP:     currentPage->onButton5Up();    break;
        case BTN_WAKE:     break;   // 串口唤醒: 已resetIdleTimer, 无页面动作
    }
}

ButtonHandle::~ButtonHandle()
{
    // 删除按钮检测器（会自动注销中断）
    if (pBtnBack)   delete pBtnBack;
    if (pBtnMenu)   delete pBtnMenu;
    if (pBtnEnter)  delete pBtnEnter;
    if (pBtn1)      delete pBtn1;
    if (pBtn2)      delete pBtn2;
    if (pBtn3)      delete pBtn3;
    if (pBtn4)      delete pBtn4;
    if (pBtn5)      delete pBtn5;
    if (pBtn6)      delete pBtn6;
    if (pBtn7)      delete pBtn7;
    if (pBtn8)      delete pBtn8;
    if (pBtn9)      delete pBtn9;

    // 删除队列
    if (buttonEventQueue != NULL) {
        vQueueDelete(buttonEventQueue);
        buttonEventQueue = NULL;
    }

    g_buttonHandle = nullptr;
}
