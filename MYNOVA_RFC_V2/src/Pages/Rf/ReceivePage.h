/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

// ReceivePage.h
#ifndef ReceivePage_h
#define ReceivePage_h

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UILabel.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../GUI/GUIRender.h"
#include "../../Drivers/RadioHelper.h"

// 接收页面状态
enum ReceivePageState {
    STATE_RECEIVING,        // 正在接收状态
    STATE_RECEIVED          // 已接收到数据状态
};

class ReceivePage : public UIPage {
public:
    ReceivePage(RFBand band);
    ReceivePage(float fixedFreqMHz);  // 单频点守候接收
    ~ReceivePage();
    
    void render(U8G2* u8g2) override;
    void showPage() override;  // 页面显示时自动开始接收
    // 重写按钮事件处理
    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;

    
private:
    void initLayout();              // 初始化页面布局
    void updateReceiveStatus();     // 更新接收状态显示
    void startReceiving();          // 开始接收
    void stopReceiving();           // 停止接收
    String formatHexData(const RFSignalData& data); // 数据HEX格式化
    
    UILabel* freqLabel;             // 当前扫描频率标签
    UILabel* dataLabel;             // 接收数据标签
    UILabel* bitLengthLabel;        // 位长度标签
    UILabel* pulseLengthLabel;      // 脉宽标签
    UILabel* protocolLabel;         // 协议标签
    UINavBar* navBar;               // 导航栏
    
    RFBand band;                    // 接收频段
    bool fixedFreq;                 // true=单频点守候
    float fixedFreqMHz;             // 单频点（MHz）
    ReceivePageState currentState;  // 当前页面状态
    unsigned long lastFreqShowTime; // 频率显示刷新时间
};

#endif
