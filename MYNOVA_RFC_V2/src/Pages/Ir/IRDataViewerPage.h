/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

#ifndef __IRDATAVIEWERPAGE_H__
#define __IRDATAVIEWERPAGE_H__

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UILabel.h"
#include "../../GUI/Widget/UITextArea.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../GUI/GUIRender.h"
#include "../../Proto/SignalData.h"

/**
 * IRDataViewerPage - 红外信号数据详情查看器（IRrecvDumpV3风格）
 *
 * 等宽5x7字体逐帧展示解码结果与原始时序，供"学习结果"与"已存槽位"
 * 两种场景共用，参照专业工具（Flipper/HackRF）的信息密度设计：
 *   F1/2 NEC 32bit
 *   VAL:1FE807F A:1F8 C:807F
 *   GAP:42ms DUR:107ms EDG:68
 *   RAW:9000,4500,560,1690,...
 *
 * 按键: 1/2=上滚  8/Menu=下滚  返回=退出
 */
class IRDataViewerPage : public UIPage {
public:
    // 引用模式：直接查看学习结果（meta/edges由IRHelper持有，须在其生命周期内使用）
    IRDataViewerPage(const IRSignalMeta* meta, const uint16_t* edges);
    // 槽位模式：从DataStore按需读取元数据与边沿数据（退出时释放）
    explicit IRDataViewerPage(int slotIndex, SignalKind kind = SIG_IR);
    ~IRDataViewerPage() override;

    void render(U8G2* u8g2) override;

    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;  // 上页（右键）
    void onButtonMenu(void* context = nullptr) override;   // 下行（中键）
    void onButton1(void* context = nullptr) override;      // 上一行
    void onButton2(void* context = nullptr) override;      // 上一行
    void onButton4(void* context = nullptr) override;      // 上一页
    void onButton6(void* context = nullptr) override;      // 下一页
    void onButton8(void* context = nullptr) override;      // 下一行

private:
    void initLayout();
    void buildDumpText();
    void appendFrameDump(String& out, uint8_t fi, uint32_t edgeBase);
    void scrollUp();
    void scrollDown();

    UITextArea* textArea;      // 可滚动dump区
    UINavBar* navBar;          // 底部导航

    IRSignalMeta metaCopy;     // 槽位模式持有的元数据副本
    const IRSignalMeta* metaRef;   // 实际展示的元数据（引用或&metaCopy）
    uint16_t* ownedEdges;      // 槽位模式持有的边沿缓冲（nullptr=引用模式）
    const uint16_t* edgesRef;  // 实际展示的边沿数据
};

#endif // __IRDATAVIEWERPAGE_H__