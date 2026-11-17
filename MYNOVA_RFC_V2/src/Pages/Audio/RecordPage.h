/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// RecordPage.h
// 声音录音页面 (SONY D100录音笔风格, 128x64 单色OLED)
//
// 架构: 与音乐播放页同款 —— 页面只发命令+读快照(无锁), 录音引擎在音频任务异步执行
//
// 输入源(进入前由RecordSourcePage选定, 本页内固定不可切):
//   1.内置麦克风(LINPUT1, AOS3729A)  → 单声道WAV(88.2KB/s, 省空间)
//   2.外置耳机麦克风(RINPUT1)        → 单声道WAV
//   3.内外双麦克风                    → 立体声WAV(176.4KB/s, L=内置/R=外置)
//   单声道源数据复制到两声道显示(电平表L/R一致), 文件仅存1声道
//
// 界面布局:
//   y1-5    L电平条(1px竖线阵列+2px峰值) | 爆VU框(3px空心, 削波变实心)
//   y8-12   R电平条 | 爆VU框                     右侧: 录音增益 "-12dB"(与L/R两行对齐)
//   y23-31  REC/PAUSE反色块 | 录音时间 "1h23m45s"(暂停时闪烁, 大字右对齐)
//   y37-41  编码 "LPCM 44.1k/16bit" | 文件大小(录音中)/SD剩余空间(待机)
//   y48     分割线
//   y56-63  左: 当前文件 "FOLDER01/REC001.WAV"(超长滚动) | 右: 电池图标
//
// 存储: TF卡 /Record/FOLDER01/RECxxx.WAV (编号自动递增, LPCM 44.1kHz/16bit)
//
// 按键:
//   2=增益+ 8=增益- (步进3dB, 范围-60~+30dB, 即调即生效且电平表联动)
//   5/ENTER=待机开始录音 / 录音中暂停 / 暂停继续
//   BACK=停止录音(落盘收尾)并返回
#ifndef RecordPage_h
#define RecordPage_h

#include "../../GUI/UIPage.h"
#include "../../GUI/GUIRender.h"
#include "../../Drivers/AudioPlayer.h"

class RecordPage : public UIPage {
public:
    explicit RecordPage(int source);   // source=RecSource(由RecordSourcePage选定)
    ~RecordPage() override;

    void update() override;
    void render(U8G2* u8g2) override;

    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;
    void onButton2(void* context = nullptr) override;
    void onButton5(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;

private:
    // 页面错误态(硬件不就绪/目录异常)
    enum RecordPageState {
        RP_OK,          // 正常(监听/录音)
        RP_NO_CARD,     // 未检测到TF卡
        RP_NO_CODEC,    // 音频芯片无响应
        RP_DIR_ERROR    // 录音目录异常或已满(REC999)
    };

    void scanNextFile();               // 确保/Record/FOLDER01存在并确定下一个文件名
    void requestReturn();              // 发停止命令并进入返回流程(等收尾完成后导航)
    void toggleRecord();               // 5键: 待机→开始 / 录音→暂停 / 暂停→继续

    void drawLevelMeter(U8G2* u8g2);   // L/R电平条+爆VU框+增益
    void drawRecRow(U8G2* u8g2);       // REC反色块+时间(暂停闪烁)
    void drawInfoRow(U8G2* u8g2);      // 编码格式+大小/剩余空间
    void drawBottomRow(U8G2* u8g2);    // 文件名(滚动)+电池
    void updateScroll();               // 文件名超长无缝滚动(停留1秒后开始)

    static int voltageToLevel(float voltage);

    RecordPageState pageState = RP_OK;
    bool armed = false;                // 已发监听(ARM)命令
    bool triedStart = false;           // 本会话已按过5键尝试启动(用于录音失败检测)
    bool returning = false;            // 返回流程中(等录音收尾)
    unsigned long returnAt = 0;        // 返回流程起始时刻(超时兜底)
    unsigned long errorAt = 0;         // 错误/收尾提示起始时刻(定时后返回)

    String fileLabel = "";             // 界面显示 "FOLDER01/REC001.WAV"
    String fullPath = "";             // TF卡路径 "/Record/FOLDER01/REC001.WAV"
    bool fileReady = false;            // 文件名已确定

    // 文件名滚动(渲染任务内更新, 无并发)
    int labelWidth = 0;                // 文件名像素宽(每帧render时计算)
    int scrollOffset = 0;
    unsigned long scrollTimer = 0;
    unsigned long scrollIdleUntil = 0; // 滚动前停留期限(进入页面1秒内不滚)

    // 暂停时间闪烁(500ms周期)
    bool blinkOn = true;
    unsigned long lastBlinkAt = 0;

    // 电池(周期刷新缓存, 不触发ADC)
    int batteryLevel = 5;
    unsigned long lastBatteryCheck = 0;

    // SD剩余空间(10秒缓存, 避免频繁查询文件系统)
    String sdFreeStr = "";
    unsigned long lastSdFreeAt = 0;
};

#endif
