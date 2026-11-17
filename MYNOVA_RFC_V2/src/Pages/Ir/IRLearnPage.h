/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

#ifndef __IRLEARNPAGE_H__
#define __IRLEARNPAGE_H__

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UILabel.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../GUI/GUIRender.h"
#include "../../Drivers/IRHelper.h"

// 红外学习页面状态
enum IRLearnPageState {
    IR_STATE_LEARNING,      // 正在学习状态
    IR_STATE_LEARNED        // 学习完成状态
};

/**
 * IRLearnPage - 红外学习页面（动态网格布局）
 *
 * 学习中：内容区居中显示"等待接收信号.../正在学习 N帧"提示文字
 * 学习完成：切换为结果画面，按协议实际提供的字段动态填充4行×左右两列网格：
 *
 *   ┌──────────────────────────────┐
 *   │ 协议:NEC          帧:1/1     │ 第1行: 协议 | 帧序号（必有）
 *   │ 值:1FE807F                   │ 第2行: 码值（必有，长值独占整行）
 *   │ 位宽:32bit        边沿:68    │ 第3行: 位宽 | 边沿数（必有）
 *   │ 地址:1F8          命令:807F  │ 第4行: 按协议动态（见下）
 *   │ 返回          详情      保存 │ 导航栏
 *   └──────────────────────────────┘
 *
 * 第4行动态策略（库提供的字段因协议而异）：
 *   - 空调协议: "状态:" + 4x6小字可读状态（库resultAcToString输出）
 *   - 有地址/命令: 地址 | 命令
 *   - 多帧普通协议: 间隔 | 时长
 *   - 单帧普通协议: 首脉宽（整行，便于分析RAW信号）
 *   - 未识别(UNKNOWN): 值为库计算的哈希，显示首脉宽辅助判断
 */
class IRLearnPage : public UIPage {
public:
    IRLearnPage(int suggestedSlot = -1);
    ~IRLearnPage();
    void setTargetSlot(int slot) { targetSlot = slot; }
    int getTargetSlot() const { return targetSlot; }

    void render(U8G2* u8g2) override;
    void showPage() override;
    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButton1(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;

private:
    void initLayout();              // 初始化页面布局
    void updateLearnStatus();       // 轮询学习状态并刷新显示
    void startLearning();           // 开始学习
    void stopLearning();            // 停止学习
    void refreshLiveRows();         // 按当前帧字段动态填充网格
    void cycleFrame(int delta);     // 逐帧切换（LEARNED状态下用）
    String frameValueHex(uint8_t fi);     // 码值HEX（空调按状态字节序）
    void hideGrid();                // 隐藏全部网格标签
    // 网格行助手：两列/整行/前缀+小字 三种填充方式
    void rowSplit(int row, const String& left, const String& right);
    void rowFull(int row, const String& text);
    void rowPrefixSmall(const String& prefix, const String& content);

    UILabel* centerLabel;           // 学习中提示（内容区居中）
    UILabel* rows[4][2];            // 4行×左右两列动态网格
    UILabel* statusContentLabel;    // 状态行内容（紧跟"状态:"前缀，4x6小字）
    UINavBar* navBar;               // 导航栏

    IRLearnPageState currentState;  // 当前页面状态
    unsigned long lastCheckTime;    // 上次检查时间
    uint8_t viewFrameIndex;         // 当前展示的帧索引（LEARNED状态可切换）
    uint8_t lastShownFrames;        // 上次刷新时的帧数（变化检测）
    uint8_t lastDotStep;            // 提示文字省略号动画步进
    int targetSlot = -1;            // 来自管理数据页的预选槽位（-1=无预选）
};

#endif
