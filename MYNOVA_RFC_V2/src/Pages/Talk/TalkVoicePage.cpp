/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// TalkVoicePage.cpp - 对讲通话页实现(音频引擎对接见AudioPlayer TALK部分)
#include "TalkVoicePage.h"
#include "../../GUI/UIEngine.h"
#include "../../Services/TalkManager.h"
#include "../../Drivers/BatteryManager.h"
#include "../../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "TALKPG"

extern UIEngine uiEngine;
extern BatteryManager batteryManager;

#define TV_FONT_SMALL   u8g2_font_profont11_tf
#define TV_FONT_TIME    u8g2_font_6x13B_tf
#define TV_FONT_CN      u8g2_font_wqy12_t_gb2312
#define TV_FONT_SIJI    u8g2_font_siji_t_6x10
// Siji icon font: 57420=麦克风(本端MIC电平) 57422=喇叭(音量)
#define TV_GLYPH_MIC    57420
#define TV_GLYPH_SPK    57422
// Waffle icon font: 57944=1格信号..57948=5格信号(标题栏同款, 此处用4档)
#define TV_GLYPH_SIG1   57944
#define TV_WAVE_X       0
#define TV_WAVE_W       77
#define TV_WAVE_Y       14
#define TV_WAVE_H       37
#define TV_PANEL_X      78

// 天线图标(同UITitleBar ant_bits)
static const unsigned char tvAntBits[] = {
   0x00, 0x00, 0xfe, 0x03, 0x24, 0x01, 0xa8, 0x00, 0x70, 0x00, 0x20, 0x00,
   0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x00, 0x00 };

// 断格电平条(同播放器VU表SONY D100风格): 电平=多根1px竖线(1px间隔)从左亮起,
// 峰值保持=2px宽竖线压在峰值位置
static void tvDrawVuRow(U8G2* u8g2, int x, int y, int w, int h, int level, int peak) {
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    if (peak < 0) peak = 0;
    if (peak > 100) peak = 100;
    const int segments = w / 2;
    int lit = (segments * level) / 100;
    for (int i = 0; i < lit; i++)
        u8g2->drawVLine(x + i * 2, y, h);
    if (peak > 0) {
        int pkX = x + (segments * peak) / 100 * 2;
        if (pkX > x + w - 2) pkX = x + w - 2;
        u8g2->drawBox(pkX, y, 2, h);
    }
}

TalkVoicePage::TalkVoicePage() : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT) {
    memset(peerName, 0, sizeof(peerName));
    memset(rxWave, 0, sizeof(rxWave));
    memset(txWave, 0, sizeof(txWave));   // 不清零→进页显示堆上残留垃圾波形且超框
}

TalkVoicePage::~TalkVoicePage() {
    if (pttOn) {
        pttOn = false;
        talkManager.setPttTalking(false);
        audioPlayer.setTalkPtt(false);
    }
    audioPlayer.stopTalk();
    talkManager.setBusy(false);   // 兜底: 自动返回路径(onButtonBack之外)也要清忙闲广播
}

void TalkVoicePage::showPage() {
    UIPage::showPage();
    talkManager.getPeerName(peerName, sizeof(peerName));
    callStartMs = millis();
    // 进通话: 开TALK音频引擎(与TalkManager模式对齐, 喇叭跟随用户speakerOn设置)
    audioPlayer.setTalkMode((int)talkManager.getMode());
    audioPlayer.startTalk();
    talkManager.setBusy(true);
}

void TalkVoicePage::startPtt() {
    // PTT模式按住说话; 全双工常开无需按住
    if (talkManager.getMode() == TALK_MODE_FULL) return;
    if (!talkManager.isPeerConnected()) return;
    if (pttOn) return;
    pttOn = true;
    talkManager.setPttTalking(true);
    audioPlayer.setTalkPtt(true);
    LOG_I("TalkVoice: PTT 按下说话");
}

void TalkVoicePage::stopPtt() {
    if (!pttOn) return;
    pttOn = false;
    talkManager.setPttTalking(false);
    audioPlayer.setTalkPtt(false);
    LOG_I("TalkVoice: PTT 松开发送");
}

void TalkVoicePage::togglePtt() {
    // ENTER键兼容Toggle(PTT模式); 全双工下无效(常开)
    if (talkManager.getMode() == TALK_MODE_FULL) return;
    if (!talkManager.isPeerConnected()) return;
    if (pttOn) stopPtt();
    else startPtt();
}

void TalkVoicePage::sendQuick(int idx) {
    if (!talkManager.isPeerConnected()) return;
    if (idx < 0 || idx >= TALK_QUICK_COUNT) return;
    // 快捷短语走CTRL_QUICK单包(对端入聊天历史+滴声由其页面处理)
    if (talkManager.sendQuick((uint8_t)idx)) SoundAlertBeep(80);
}

void TalkVoicePage::update() {
    // 对端离开/掉线 → 立即停发并提示, 随后自动返回上一页
    if (!returning && !talkManager.isPeerConnected()) {
        returning = true;
        returnAt = millis();
        dropReason = talkManager.getLinkDropReason();
        if (pttOn) stopPtt();            // 不等800ms: 看门狗置READY后立即停MIC发送
        talkManager.setBusy(false);
        SoundAlertBeep(200);             // 断线提示音(正常离开/掉线均提醒)
    }
    if (returning) {
        if (millis() - returnAt > 1500) uiEngine.navigateBack();
        return;
    }
    // 对端切模式(CTRL同步) → 本端音频引擎跟随(本端发起的切本地已设, 重复设无害)
    {
        static int lastMode = -1;
        int m = (int)talkManager.getMode();
        if (m != lastMode) {
            lastMode = m;
            if (pttOn) stopPtt();   // 切到全双工前先松PTT(防按住态残留)
            audioPlayer.setTalkMode(m);
        }
    }
    // 低电提示(<3.4V): 2.4G+双工功耗大, 弹一次提示(页面内不再重复)
    if (!lowBatWarned && batteryManager.getLastVoltage() < 3.4f &&
        batteryManager.getLastVoltage() > 2.5f) {   // >2.5V过滤未采样初值
        lowBatWarned = true;
        SoundAlertBeep(200);
    }
    // 通话中收到对端文字/快捷短语 → 滴声提醒并置已读(历史在聊天页可见)
    if (talkManager.hasUnread()) {
        talkManager.markRead();
        SoundAlertBeep(100);
    }
    // 远端语音首包 → 滴声提示(来音提醒, 500ms内只提示一次由时间窗口保证)
    static unsigned long lastBeepMs = 0;
    if (talkManager.hasRxVoice(300) && millis() - lastBeepMs > 3000) {
        // 通话中不频繁滴声, 仅首包提示(来音)
        if (millis() - callStartMs < 3000) {
            lastBeepMs = millis();
            SoundAlertBeep(80);
        }
    }
    if (millis() - lastBatMs > 2000) {
        lastBatMs = millis();
        batteryLevel = voltageToLevel(batteryManager.getLastVoltage());
    }
}

void TalkVoicePage::render(U8G2* u8g2) {
    if (returning) {
        u8g2->setFont(TV_FONT_CN);
        if (dropReason == TALK_DROP_LOST) {
            // 异常掉线(对端关机/超出距离): 两行明确提示, 区别于正常离开
            const char* l1 = "连接已断开";
            const char* l2 = "对方关机或超出距离";
            uint16_t w1 = u8g2->getUTF8Width(l1);
            uint16_t w2 = u8g2->getUTF8Width(l2);
            u8g2->drawUTF8((SCREEN_WIDTH - w1) / 2, 27, l1);
            u8g2->drawUTF8((SCREEN_WIDTH - w2) / 2, 43, l2);
        } else {
            const char* msg = "对方已离开...";
            uint16_t w = u8g2->getUTF8Width(msg);
            u8g2->drawUTF8((SCREEN_WIDTH - w) / 2, 34, msg);
        }
        return;
    }
    // 采样双通道波形历史(每渲染帧一次), 同时维护远端峰值保持(每帧-1慢衰减)
    uint8_t spk = audioPlayer.getSpkLevel();
    bool full = (talkManager.getMode() == TALK_MODE_FULL);
    bool talking = (full || pttOn) && !muted;
    rxWave[waveIdx] = spk;
    txWave[waveIdx] = talking ? audioPlayer.getVuL() : 0;
    waveIdx = (waveIdx + 1) % sizeof(rxWave);
    if (spk > spkPeak) spkPeak = spk;
    else if (spkPeak > 0) spkPeak--;
    drawTopRow(u8g2);
    u8g2->drawHLine(0, 12, SCREEN_WIDTH);
    drawWaveBox(u8g2);
    drawRightPanel(u8g2);
    u8g2->drawHLine(0, 53, SCREEN_WIDTH);
    drawBottomRow(u8g2);
}

void TalkVoicePage::drawTopRow(U8G2* u8g2) {
    // 第1行: 对端名(左,中文)+MUTE提示(中右)+天线+信号强度图标(右,同首页标题栏)
    u8g2->setFont(TV_FONT_CN);
    u8g2->drawUTF8(0, 11, peerName[0] ? peerName : "-");
    const int sigW = 16;   // 天线+信号格组合宽(天线10 + 信号格偏移6)
    if (muted) {
        u8g2->setFont(TV_FONT_SMALL);
        const char* m = "MUTE";
        int mw = u8g2->getStrWidth(m);
        u8g2->drawStr(SCREEN_WIDTH - sigW - 3 - mw, 11, m);
    }
    // 信号强度: 天线XBM+信号格(复用首页标题栏同款), RSSI取自ESP-NOW收包
    u8g2->drawXBM(SCREEN_WIDTH - sigW, 1, 10, 10, tvAntBits);
    u8g2->setFont(u8g2_font_waffle_t_all);
    u8g2->drawGlyph(SCREEN_WIDTH - sigW + 6, 11, TV_GLYPH_SIG1 + rssiToBars(talkManager.getPeerRssi()) - 1);
}

void TalkVoicePage::drawWaveBox(U8G2* u8g2) {
    // 左半双通道波形区:上通道=远端RX(喇叭), 下通道=本端TX(麦克风), 均从右向左滚动
    // 与右侧面板行序一致(喇叭上/麦克下); 缓冲与可视列数等宽, 波形可一直滚动到左边框
    u8g2->drawFrame(TV_WAVE_X, TV_WAVE_Y, TV_WAVE_W, TV_WAVE_H);
    const int half = 8;                       // 通道半幅(v=100时占满±8)
    const int midRx = TV_WAVE_Y + 9;          // 23: 上通道中线(占15..31)
    const int midTx = TV_WAVE_Y + 27;         // 41: 下通道中线(占33..49)
    u8g2->drawHLine(TV_WAVE_X + 1, midRx, TV_WAVE_W - 2);
    u8g2->drawHLine(TV_WAVE_X + 1, midTx, TV_WAVE_W - 2);
    const int N = (int)sizeof(rxWave);
    for (int x = 0; x < TV_WAVE_W - 2; x++) {
        int k = ((int)waveIdx - 1 - x) % N;   // x=0最新(最右), 越往左越旧
        if (k < 0) k += N;
        int px = TV_WAVE_X + 1 + (TV_WAVE_W - 3 - x);
        int hr = (rxWave[k] * half) / 100;    // 远端RX
        if (hr > half) hr = half;             // 防御: 电平>100时不出框
        if (hr <= 0) u8g2->drawPixel(px, midRx);
        else u8g2->drawVLine(px, midRx - hr, hr * 2);
        int ht = (txWave[k] * half) / 100;    // 本端TX
        if (ht > half) ht = half;
        if (ht <= 0) u8g2->drawPixel(px, midTx);
        else u8g2->drawVLine(px, midTx - ht, ht * 2);
    }
    // TX/RX徽标:叠加在波形区左上角(有收发时显示)
    bool full = (talkManager.getMode() == TALK_MODE_FULL);
    bool tx = full ? !muted : (pttOn && !muted);
    bool rx = talkManager.hasRxVoice(400);
    if (tx || rx) {
        const char* tag = tx ? "TX" : "RX";
        u8g2->setFont(TV_FONT_SMALL);
        int tw = u8g2->getStrWidth(tag);
        u8g2->drawBox(TV_WAVE_X + 2, TV_WAVE_Y + 2, tw + 6, 11);
        u8g2->setFontMode(1);
        u8g2->setDrawColor(0);
        u8g2->drawStr(TV_WAVE_X + 5, TV_WAVE_Y + 10, tag);
        u8g2->setDrawColor(1);
        u8g2->setFontMode(0);
    }
}

void TalkVoicePage::drawRightPanel(U8G2* u8g2) {
    // 右侧:大时间hh:mm:ss固定格式(8字符48px<面板50px, 放得下)+两行电平(图标左+断格电平条右)
    uint32_t s = (millis() - callStartMs) / 1000;
    char tbuf[12];
    snprintf(tbuf, sizeof(tbuf), "%02lu:%02lu:%02lu",
             (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
    u8g2->setFont(TV_FONT_TIME);
    int tw = u8g2->getStrWidth(tbuf);
    u8g2->drawStr(TV_PANEL_X + (SCREEN_WIDTH - TV_PANEL_X - tw) / 2, TV_WAVE_Y + 13, tbuf);
    // 两行电平:喇叭(远端RX)+麦克风(本端MIC); 断格电平条高8px, 与左侧图标墨迹带平齐(条y=基线-8)
    const int barX = TV_PANEL_X + 10;               // 电平条起点(图标右侧)
    const int barW = SCREEN_WIDTH - barX - 1;
    const int barH = 8;
    bool full = (talkManager.getMode() == TALK_MODE_FULL);
    bool talking = (full || pttOn) && !muted;   // 静音/未讲话时麦克风条为空
    u8g2->setFont(TV_FONT_SIJI);
    // 第1行:喇叭图标+远端断格电平条(峰值保持)
    const int spkBase = TV_WAVE_Y + 24;             // 图标基线38
    u8g2->drawGlyph(TV_PANEL_X, spkBase, TV_GLYPH_SPK);
    tvDrawVuRow(u8g2, barX, spkBase - barH, barW, barH,
                audioPlayer.getSpkLevel(), spkPeak);
    // 第2行:麦克风图标+本端断格电平条(峰值保持)
    const int micBase = TV_WAVE_Y + 36;             // 图标基线50
    u8g2->drawGlyph(TV_PANEL_X, micBase, TV_GLYPH_MIC);
    tvDrawVuRow(u8g2, barX, micBase - barH, barW, barH,
                talking ? audioPlayer.getVuL() : 0,
                talking ? audioPlayer.getPeakL() : 0);
}

void TalkVoicePage::drawBottomRow(U8G2* u8g2) {
    // 底部:喇叭图标+音量%+麦克风图标+增益dB(左)+电池(右)
    u8g2->setFont(TV_FONT_SIJI);
    u8g2->drawGlyph(0, 62, TV_GLYPH_SPK);
    char vbuf[10];
    snprintf(vbuf, sizeof(vbuf), "%d%%", audioPlayer.getVolume());
    u8g2->setFont(TV_FONT_SMALL);
    u8g2->drawStr(10, 62, vbuf);
    int vw = u8g2->getStrWidth(vbuf);
    u8g2->setFont(TV_FONT_SIJI);
    u8g2->drawGlyph(12 + vw, 62, TV_GLYPH_MIC);
    char gbuf[12];
    int g = audioPlayer.getRecGainDb();
    if (g > 0) snprintf(gbuf, sizeof(gbuf), "+%ddB", g);
    else snprintf(gbuf, sizeof(gbuf), "%ddB", g);
    u8g2->setFont(TV_FONT_SMALL);
    u8g2->drawStr(22 + vw, 62, gbuf);
    u8g2->setFont(u8g2_font_battery19_tn);
    u8g2->setFontDirection(3);
    u8g2->drawGlyph(SCREEN_WIDTH, 62, 48 + batteryLevel);
    u8g2->setFontDirection(0);
}

int TalkVoicePage::voltageToLevel(float voltage) {
    if (voltage >= 4.0) return 5;
    if (voltage >= 3.8) return 4;
    if (voltage >= 3.6) return 3;
    if (voltage >= 3.4) return 2;
    if (voltage >= 3.2) return 1;
    return 0;
}

int TalkVoicePage::rssiToBars(int8_t rssi) {
    // RSSI(dBm)→1..4格信号(ESP-NOW常见范围: 同房间-40~-60, 隔墙-65~-85)
    if (rssi >= -50) return 4;   // 4格: 很强
    if (rssi >= -62) return 3;   // 3格: 良好
    if (rssi >= -74) return 2;   // 2格: 一般
    return 1;                    // 1格: 弱
}

void TalkVoicePage::onButtonBack(void* context) {
    if (returning) return;
    returning = true;
    returnAt = millis() - 700;   // 快速返回菜单
    stopPtt();
    talkManager.setBusy(false);
    audioPlayer.stopTalk();
    // 保持连接(不断开): 回菜单后仍可进对讲/聊天/设置, 断开请到1.连接设备页
    uiEngine.navigateBack();
}

void TalkVoicePage::onButtonEnter(void* context) {
    togglePtt();   // ENTER兼容Toggle(PTT模式); 5键走按住说话沿事件
}

void TalkVoicePage::onButtonMenu(void* context) {
}

void TalkVoicePage::onButton1(void* context) {
    muted = !muted;
    audioPlayer.setTalkMuted(muted);
}

void TalkVoicePage::onButton3(void* context) {
    audioPlayer.setVolume(audioPlayer.getVolume() + 5);
}

void TalkVoicePage::onButton2(void* context) {
    audioPlayer.setVolume(audioPlayer.getVolume() + 5);
}

void TalkVoicePage::onButton8(void* context) {
    audioPlayer.setVolume(audioPlayer.getVolume() - 5);
}

void TalkVoicePage::onButton4(void* context) {
    audioPlayer.setRecGain(audioPlayer.getRecGainDb() + REC_GAIN_STEP);
}

void TalkVoicePage::onButton5(void* context) {
    // 单击忽略(按住说话走Down/Up沿事件, 防单击与长按重复触发)
}

void TalkVoicePage::onButton5Down(void* context) {
    startPtt();   // 按住5说话
}

void TalkVoicePage::onButton5Up(void* context) {
    stopPtt();    // 松开发送
}

void TalkVoicePage::onButton6(void* context) {
    audioPlayer.setRecGain(audioPlayer.getRecGainDb() - REC_GAIN_STEP);
}

void TalkVoicePage::onButton7(void* context) {
    sendQuick(1);   // "收到!"
}

void TalkVoicePage::onButton9(void* context) {
    sendQuick(0);   // "喂,能听到吗?"
}
