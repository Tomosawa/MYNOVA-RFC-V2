/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef __SENDDATAPAGE_H__
#define __SENDDATAPAGE_H__

#include <Arduino.h>
#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIMenu.h"
#include "../../GUI/Widget/UIMessageBox.h"
#include "../../GUI/Widget/UILabel.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../Services/DataStore.h"
#include "../../Proto/SignalData.h"

enum SendDataPageState {
    STATE_SELECT,            // 选择数据状态
    STATE_DETAIL,            // 显示详情状态
    STATE_SENDING,           // 发送中状态
    STATE_SET_QUICK_PROFILE, // 选择要绑定的主页
    STATE_SET_QUICK          // 选择按键 1-9
};

enum SendFilterMode {
    FILTER_ALL = 0,    // 全部信号（射频+红外）
    FILTER_RF,         // 仅射频
    FILTER_IR          // 仅红外
};

/**
 * SendDataPage - 发送模式页面（射频/红外统一，动态网格布局）
 * 详情页采用4行×左右两列网格，值长内容独占整行，状态英文小字紧跟前缀。
 * RF/IR 按协议实际字段动态填充，避免固定字段造成的空位。
 */
class SendDataPage : public UIPage {
public:
    SendDataPage(SendFilterMode filter = FILTER_ALL);
    ~SendDataPage();
    
    void initLayout();
    void render(U8G2* u8g2) override;
    void update() override;
    
    // 按钮事件处理
    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButton1(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton3(void* context = nullptr) override;
    void onButton4(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton6(void* context = nullptr) override;
    void onButton7(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;
    void onButton9(void* context = nullptr) override;

private:
    UIMenu* dataListMenu;
    UIMenu* quickProfileMenu = nullptr;
    int quickProfileId = -1;
    UIMessageBox* messageBox;
    UIMessageBox* quickSetBox;
    // 详情网格（4行×2列）+ 状态小字
    UILabel* rows[4][2];
    UILabel* statusContentLabel;
    // 兼容保留（旧布局引用，隐藏不用于新网格）
    UILabel* freqLabel;
    UILabel* dataLabel;
    UILabel* bitLengthLabel;
    UILabel* pulseLengthLabel;
    UILabel* typeLabel;
    UILabel* nameLabel;
    UINavBar* detailNavBar;
    UINavBar* quickSetNavBar;
    
    SendFilterMode filter;          // 列表过滤类型
    int slotList[RF_SLOT_COUNT + IR_SLOT_COUNT]; // 菜单项 -> 编码槽位(1..100 RF, 101..200 IR) 映射
    int slotListCount;              // 有效菜单项数量
    
    SendDataPageState currentState;
    int selectedDataIndex;          // 当前选中的编码槽位(1..100 RF, 101..200 IR)
    SignalEntry currentData;
    bool bAnimating;
    uint8_t irViewFrame;            // 红外详情当前展示的帧索引（上下键切换）

    void showDataDetail(int slotIndex);
    void setAsQuickKey(int keyIndex);
    void enterQuickProfilePick();
    void enterQuickKeyPick();
    bool rawSending = false;
    bool sendCurrentSignal();
    void refreshIRDetailRows();     // 刷新红外详情的动态网格
    void refreshRFDetailRows();     // 刷新射频详情的动态网格
    void cycleIRFrame(int delta);   // 红外详情逐帧切换
    void hideDetailGrid();
    void rowSplit(int row, const String& left, const String& right);
    void rowFull(int row, const String& text);
    void rowPrefixSmall(const String& prefix, const String& content);
    String irFrameValueHex(uint8_t fi);
    String formatHexData(const RFSignalData& data);
};

#endif
