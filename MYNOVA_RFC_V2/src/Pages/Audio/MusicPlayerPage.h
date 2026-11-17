/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// MusicPlayerPage.h
// 专业音乐播放页面 (128x64 单色OLED)
//
// 架构: 页面只发命令+读快照(无锁), 切歌/暂停等由音频任务异步执行, UI永不阻塞
//
// 界面布局:
//   y0-10   状态栏: 循环模式 | 睡眠时钟 | 音量(喇叭+百分比) | 曲目序号 | 电池
//   y11     分隔线
//   y12-25  歌曲名称(居中, 超长无缝连续滚动)
//   y26-35  左: 格式+文件大小(第一行)    右: 电平L(无背景, 1px竖线阵列, 峰值2px宽)
//   y36-45  左: 采样率+位深(第二行)      右: 电平R
//   y48-53  播放进度条
//   y54-63  当前时间(最左) | 播放状态图标(居中) | 总时长(最右)
//
// 按键:
//   1=循环模式 2=音量+ 8=音量- 4=上一曲 6=下一曲
//   5/ENTER=播放暂停 3=视图切换(默认→频谱→指针VU→波形→默认) BACK=停止并返回
//   MENU=睡眠定时菜单(关闭/10/20/30/45/60分/自定义), 自定义用数字键1-9输入、ENTER确定、BACK删除
#ifndef MusicPlayerPage_h
#define MusicPlayerPage_h

#include "../../GUI/UIPage.h"
#include "../../GUI/GUIRender.h"
#include "../../Drivers/AudioPlayer.h"
#include <vector>

enum ViewMode {
    VIEW_DEFAULT = 0,
    VIEW_SPECTRUM = 1,
    VIEW_ANALOG_VU = 2,
    VIEW_WAVEFORM = 3
};

class MusicPlayerPage : public UIPage {
public:
    MusicPlayerPage(std::vector<String> fileList, int startIndex);
    ~MusicPlayerPage() override;

    void update() override;
    void render(U8G2* u8g2) override;

    void onButtonBack(void* context = nullptr) override;
    void onButtonMenu(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
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
    // 按键动作实现(按键任务内直接执行; audioPlayer命令接口全部非阻塞入队, 无并发风险)
    void togglePlayPause();            // 播放/暂停/重新播放
    void nextTrack();                  // 下一曲
    void prevTrack();                  // 上一曲
    void cyclePlayMode();              // 1键: 循环模式切换
    void requestReturn();              // 发停止命令并进入返回流程(等空闲后导航)

    // 睡眠定时(菜单键)
    void openSleepMenu();              // 打开睡眠选项菜单
    void moveSleepSel(int delta);      // 菜单内移动光标(2/8键)
    void sleepSelectCurrent();         // 确定当前菜单项
    void acceptSleepInput();           // 确定自定义分钟数(ENTER)
    void appendSleepDigit(char d);     // 自定义输入追加数字(1-9键)
    void applySleep(int minutes);      // 应用定时(0=关闭)
    void startSleepShutdown();         // 到点: 显示提示并停机
    void drawSleepMenuUI(U8G2* u8g2);  // 睡眠选项菜单绘制
    void drawSleepInputUI(U8G2* u8g2); // 自定义分钟输入绘制

    void onTrackChanged();              // 曲目变化: 更新歌名并重置滚动
    void updateScroll();

    void drawStatusBar(U8G2* u8g2);
    void drawSongTitle(U8G2* u8g2);
    void drawInfoLine(U8G2* u8g2);
    void drawVUMeter(U8G2* u8g2);       // L/R电平表 + 峰值保持
    void drawPlaybackRow(U8G2* u8g2);   // 进度条(上) + 播放状态图标/时间(下)

    void cycleView();                   // 3键: 四视图循环
    void updateSpectrum();              // 频谱: FFT + 12带映射与平滑
    void updateWaveform();              // 波形: 取立体声快照并降采样
    void drawSpectrumView(U8G2* u8g2);  // 频谱视图
    void drawAnalogVuView(U8G2* u8g2);  // 指针VU视图(左右分栏)
    void drawWaveformView(U8G2* u8g2);  // 波形视图(上下分栏 L上R下)
    void drawAnalogMeter(U8G2* u8g2, int cx, int cy, int r, int pct, int peak, const char* label);

    static String formatTimeMs(uint32_t ms);
    static int voltageToLevel(float voltage);
    static String nameOf(const String& path);   // 去路径去扩展名

    std::vector<String> fileList;      // 播放列表(与AudioPlayer内部副本同内容, 索引一致)
    int currentIndex = 0;

    // 返回流程(等待音频任务停止后导航, 防止与浏览器页的文件访问并发)
    bool returning = false;
    unsigned long returnRequestedAt = 0;
    bool sawPlayback = false;          // 已观测到起播(区分"初始装载中"与"播完停止")

    // 启动失败提示(全部曲目启动失败)
    unsigned long startFailedAt = 0;

    String songName = "";
    int songNameWidth = 0;
    int scrollOffset = 0;
    unsigned long scrollTimer = 0;

    // 状态栏提示闪显(循环模式/睡眠定时设置结果)
    String flashMsg = "";
    unsigned long flashUntil = 0;
    int batteryLevel = 5;
    unsigned long lastBatteryCheck = 0;

    // 总时长兜底: 头部解析不出时按文件进度一次性推算并冻结
    uint32_t frozenTotalMs = 0;

    // 睡眠定时状态
    bool sleepMenuOpen = false;        // 睡眠选项菜单是否打开
    bool sleepInputOpen = false;       // 自定义分钟输入是否打开
    int sleepSel = 0;                  // 菜单光标索引(0..6)
    int sleepScroll = 0;               // 菜单首可见项索引
    String sleepBuf = "";              // 自定义输入内容(数字字符串)
    int sleepMinutes = 0;              // 定时分钟数(0=关闭)
    unsigned long sleepDeadline = 0;   // 到点时刻(millis)
    bool sleepFired = false;           // 到点动作已触发(防重复)
    unsigned long sleepShutdownAt = 0; // 已显示提示, 延时后进入深度休眠的时刻(0=未开始)

    // 视图切换(3键四态循环)
    ViewMode viewMode = VIEW_DEFAULT;
    uint8_t specBands[12] = {0};
    uint8_t specPeak[12] = {0};
    float vuSmoothL = 0, vuSmoothR = 0;   // 指针VU平滑值(0..100)
    unsigned long lastSpecMs = 0;
    unsigned long lastWaveMs = 0;
    // 波形视图: 128点/通道(512点降采样到128)
    int16_t waveL[128] = {0};
    int16_t waveR[128] = {0};
};

#endif
