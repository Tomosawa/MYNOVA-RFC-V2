/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// MusicBrowserPage.h
// TF卡音乐文件浏览页面: 目录导航 + 音频文件列表
// 按键: 2=上移 8/MENU=下移 5/ENTER=进入文件夹或播放 BACK=返回上级
#ifndef MusicBrowserPage_h
#define MusicBrowserPage_h

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../GUI/GUIRender.h"
#include <vector>

class MusicBrowserPage : public UIPage {
public:
    MusicBrowserPage();
    ~MusicBrowserPage() override;

    void showPage() override;
    void update() override;
    void render(U8G2* u8g2) override;

    void onButtonBack(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton4(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton6(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;

private:
    // 按键动作请求: 按键任务只投递, 由渲染任务的update()统一执行
    // (目录扫描/entries向量/导航栈的变更必须与render同任务, 否则会与渲染并发崩溃)
    enum BrowserAction {
        BA_NONE = 0,
        BA_UP,        // 光标上移
        BA_DOWN,      // 光标下移
        BA_PAGEUP,    // 上翻页
        BA_PAGEDOWN,  // 下翻页
        BA_ENTER,     // 进入文件夹/播放
        BA_BACK,      // 返回上级/退出
        BA_RETRY      // 重新检测(热插拔)
    };

    enum BrowserState {
        BROWSER_INIT,     // 正在初始化TF卡/音频硬件
        BROWSER_NO_CARD,  // 未检测到TF卡
        BROWSER_NO_CODEC, // 音频芯片(WM8960)无响应
        BROWSER_BROWSE    // 文件浏览
    };

    struct DirEntry {
        String name;
        bool isDir;
    };

    // 进入子目录前的浏览位置(返回上级时恢复光标与视口)
    struct NavPos {
        int cursor;
        int viewStart;
    };

    void initHardware();          // 初始化TF卡并进入对应状态(音频硬件延迟到播放时开启)
    void loadDirectory();         // 扫描currentPath(文件夹优先, 按名称排序)
    void moveCursor(int delta);   // 移动光标(视口跟随+首尾循环)
    void pageUp();                // 上翻一页(光标保持视口内相对位置)
    void pageDown();              // 下翻一页
    void enterItem();             // 进入文件夹 或 播放音频文件
    void goBack();                // 返回上级目录(根目录则退出页面)
    void restoreNavPos();         // 返回上级后恢复压栈的浏览位置(越界收敛+视口跟随)
    void retryInit();             // 重新检测(支持热插拔)
    void handleBack();            // 统一BACK处理(浏览态=返回上级, 错误态=退出页面)

    void drawBrowseUI(U8G2* u8g2);
    void drawErrorUI(U8G2* u8g2, const char* title, const char* hint);
    void updateSelScroll();          // 推进选中项长名字滚动(渲染任务内调用)

    String joinPath(const String& dir, const String& name) const;

    BrowserState browserState = BROWSER_INIT;
    String currentPath = "/";   // 相对挂载点的当前目录
    std::vector<DirEntry> entries;
    std::vector<NavPos> navStack;   // 目录层级位置栈(进入压栈, 返回弹栈恢复)
    int cursor = 0;             // 光标在entries中的索引
    int viewStart = 0;          // 视口起始索引
    int loadRetries = 0;        // 目录加载失败重试计数(防死循环)
    UINavBar* navBar = nullptr; // 由UIPage基类负责释放

    // 选中项长名字滚动状态(render与update均在渲染任务内, 无并发)
    String selFullName = "";    // 当前选中项完整名(变化时重置滚动)
    int selNameWidth = 0;       // 完整名像素宽度(每帧render时计算)
    int selScrollOffset = 0;    // 滚动偏移
    int selScrollPhase = 0;     // 0=起始停留 1=滚动 2=尾部停留
    unsigned long selScrollTimer = 0;

    volatile int pendingAction = BA_NONE;  // 按键任务投递的动作请求
    bool initPending = false;              // showPage仅置位, 由渲染任务的update()执行扫描(避免卡在上个页面)
    unsigned long initPendingAt = 0;       // initPending置位时刻(保证loading提示先刷屏再开始耗时扫描)

    static const int VISIBLE_ROWS = 4;
    static const int NAME_VIEW_W = 100;   // 选中行滚动可视宽度(裁剪窗口)
    static const int INIT_MIN_LOADING_MS = 350;  // "读取TF卡中..."提示最少展示时长
};

#endif
