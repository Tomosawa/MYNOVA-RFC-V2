/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// TalkChatPage.h - 文字聊天页(终端式历史+按需出现的输入栏)
// 布局128x64(无标题栏, 全屏消息区): 浏览态4行消息 / 输入态3行消息+1行输入栏
//             底部UINavBar: 左=返回 中=按5输入/请输入 右=发送
// 按键: 5=进入输入(输入栏展开) ENTER/右键=发送 1-9=T9输入 MENU=删除字符
//       浏览态: 2/8=上下单行滚动 4/6=上下整页翻 7/9=快捷短语 BACK=返回
#ifndef TalkChatPage_h
#define TalkChatPage_h

#include <vector>
#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UIInput.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../GUI/GUIRender.h"

class TalkChatPage : public UIPage {
public:
    TalkChatPage();
    ~TalkChatPage() override;

    void showPage() override;
    void update() override;
    void render(U8G2* u8g2) override;

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
    enum ChatMode { MODE_BROWSE, MODE_INPUT };  // 浏览(输入栏隐藏)/输入(输入栏展开)
    void sendCurrent();            // 发送输入框内容(发送后收起输入栏)
    void enterInput(int feedKey = -1);  // 进入输入模式(可选喂一个T9键)
    void exitToBrowse(bool keepDraft = true);  // 回浏览模式(收起输入栏)
    void syncMiddleHint();         // 底部中间提示跟随模式/连接态
    void rebuildLines(U8G2* u8g2); // 历史折行成行列表(旧→新, 按签名缓存)
    void drawHistory(U8G2* u8g2);  // 终端式历史: 最新在底部, 历史向上
    void drawInput(U8G2* u8g2);    // 输入栏(仅输入模式绘制)

    UIInput* input = nullptr;      // T9输入逻辑载体(不直接render, 自绘)
    UINavBar* navBar = nullptr;
    ChatMode mode = MODE_BROWSE;   // 默认浏览模式(输入栏隐藏)
    std::vector<String> chatLines; // 折行后的行(含收:或发:前缀, 旧→新)
    int viewOffset = 0;            // 从底部向上的滚动行数(0=跟随最新)
    int lineTotal = 0;             // 折行总行数(render时刷新, 滚动键用)
    int linesSigCount = -1;        // 行缓存签名: 历史条数
    unsigned long linesSigMs = 0;  // 行缓存签名: 最新一条消息时间戳
    unsigned long lastTextMs = 0;  // 新消息滴声
    bool lastLink = false;         // 连接态变化跟踪(中间提示未连接)
};

#endif
