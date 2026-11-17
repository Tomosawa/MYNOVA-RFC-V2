/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// TalkVoicePage.h - 对讲通话页(PTT半双工/全双工)
// 按键: 5=按住说话松开发送(Down/Up沿事件) ENTER=PTT Toggle兼容 BACK=返回菜单(保持连接)
//       2/8=喇叭音量± 4/6=麦克风增益± 1=静音MUTE 3=音量+ 7/9=快捷短语
#ifndef TalkVoicePage_h
#define TalkVoicePage_h

#include "../../GUI/UIPage.h"
#include "../../GUI/GUIRender.h"
#include "../../Drivers/AudioPlayer.h"

class TalkVoicePage : public UIPage {
public:
    TalkVoicePage();
    ~TalkVoicePage() override;

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
    void onButton5Down(void* context = nullptr) override;   // 按住5说话(按下)
    void onButton5Up(void* context = nullptr) override;     // 按住5说话(松开发送)
    void onButton6(void* context = nullptr) override;
    void onButton7(void* context = nullptr) override;
    void onButton8(void* context = nullptr) override;
    void onButton9(void* context = nullptr) override;

private:
    void startPtt();                   // 按住说话(开始)
    void stopPtt();                    // 松开发送(结束)
    void togglePtt();                  // ENTER键兼容Toggle(PTT模式)
    void sendQuick(int idx);           // 快捷短语
    void drawTopRow(U8G2* u8g2);       // 对端名+MUTE+天线+信号强度图标
    void drawWaveBox(U8G2* u8g2);       // 左半双通道波形(上=远端RX,下=本端TX)+TX/RX徽标(左上角叠加)
    void drawRightPanel(U8G2* u8g2);    // 右侧:大时间hh:mm:ss+喇叭/麦克风两行断格电平条(带峰值)
    void drawBottomRow(U8G2* u8g2);    // 喇叭图标+音量%+麦克风图标+增益dB(左)+电池(右)
    static int rssiToBars(int8_t rssi); // RSSI→1..4格信号

    bool pttOn = false;                // 本端PTT按下态(按住5期间为true)
    bool muted = false;                // 本端静音(不发语音)
    uint8_t rxWave[75];                // 远端RX波形历史(0..100, 每渲染帧采样一次; 75=TV_WAVE_W-2, 与波形可视列数等宽)
    uint8_t txWave[75];                // 本端TX波形历史(0..100, 说话时采样MIC电平, 同上等宽)
    uint8_t spkPeak = 0;               // 远端电平峰值保持(页内每帧-1慢衰减, 供断格电平条)
    uint8_t waveIdx = 0;               // 波形环形缓冲写索引(rx/tx共用, 同帧成对采样)
    bool returning = false;
    uint8_t dropReason = 0;            // 断线原因(TalkLinkDrop, 边沿锁定, 控制提示文案)
    bool lowBatWarned = false;         // 低电提示已弹过(页面内一次)
    unsigned long returnAt = 0;
    unsigned long callStartMs = 0;     // 连接成功时刻(计时)
    unsigned long lastBatMs = 0;
    int batteryLevel = 5;
    static int voltageToLevel(float voltage);
    char peerName[TALK_DEVNAME_MAX + 1];
};

#endif
