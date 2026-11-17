/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// MusicPlayerPage.cpp
// 按键: 1=循环模式 2=音量+ 8=音量- 4=上一曲 6=下一曲 5/ENTER=播放暂停 3=视图切换(默认→频谱→指针VU→波形→默认)
//       MENU=睡眠定时 BACK=停止并返回
#include "MusicPlayerPage.h"
#include "../../GUI/UIEngine.h"
#include "../../Drivers/AudioPlayer.h"
#include "../../Drivers/BatteryManager.h"
#include "../../Services/SystemSetting.h"
#include "arduinoFFT.h"
#include <math.h>
#include "../../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "MUSIC"

extern UIEngine uiEngine;
extern BatteryManager batteryManager;
extern SystemSetting systemSetting;

// ================= Siji字体图标 =================
// 播放57498 暂停57499 时钟57366 (文件夹57814/音符57970等见浏览页)
#define GLYPH_CLOCK 57366
#define GLYPH_PLAY  57498
#define GLYPH_PAUSE 57499
// 循环模式: 顺序播放 →| (10x10)
static const unsigned char mode_seq_bits[] U8X8_PROGMEM = {
    0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x80, 0x02, 0xFF, 0x02,
    0xFF, 0x02, 0x80, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02
};
// 循环模式: 列表循环 (10x10)
static const unsigned char mode_all_bits[] U8X8_PROGMEM = {
    0x00, 0x00, 0xFE, 0x00, 0x00, 0x01, 0x80, 0x03, 0x00, 0x01,
    0x02, 0x00, 0x07, 0x00, 0x02, 0x00, 0xFC, 0x01, 0x00, 0x00
};
// 循环模式: 单曲循环 (10x10)
static const unsigned char mode_one_bits[] U8X8_PROGMEM = {
    0x00, 0x00, 0xFE, 0x00, 0x00, 0x01, 0xA0, 0x03, 0x30, 0x01,
    0x22, 0x00, 0x27, 0x00, 0x02, 0x00, 0xFC, 0x01, 0x00, 0x00
};
// 循环模式: 随机播放 (10x10)
static const unsigned char mode_rand_bits[] U8X8_PROGMEM = {
    0x00, 0x01, 0x81, 0x02, 0x42, 0x01, 0x24, 0x00, 0x18, 0x00,
    0x18, 0x00, 0x24, 0x00, 0x42, 0x00, 0x81, 0x00, 0x00, 0x01
};

static const unsigned char* modeIcons[] = { mode_seq_bits, mode_all_bits, mode_one_bits, mode_rand_bits };
static const char* modeNames[] = { "顺序播放", "列表循环", "单曲循环", "随机播放" };
static const char* viewNames[] = { "默认视图", "频谱分析", "指针VU表", "波形显示" };

// 频谱标签(最后一个HI代表>16K)
static const char* specLabels[12] = { "50","100","160","250","500","750","1K","2K","4K","8K","16K","HI" };

// 睡眠定时选项(与PowerSavePage预设一致): 关闭/10/20/30/45/60分/自定义(-1表示自定义)
static const char* sleepLabels[] = { "关闭", "10分", "20分", "30分", "45分", "60分", "自定义" };
static const int sleepOptMinutes[] = { 0, 10, 20, 30, 45, 60, -1 };
static const int SLEEP_OPT_COUNT = 7;
static const int SLEEP_VISIBLE = 4;    // 菜单一屏显示行数

// 状态栏布局: 模式x2-12 | 睡眠时钟x14-21(开启时) | 喇叭x24 | 音量%x38-62 | 曲目序号(右对齐≤104) | 电池x109-128
#define ICON_MODE_X     2
#define ICON_SLEEP_X    14
#define ICON_SPK_X      24
#define VOLBAR_X        38
#define TRACK_RIGHT     104

#define FONT_SMALL      u8g2_font_profont11_tf
#define FONT_TINY       u8g2_font_3x5im_mr
#define FONT_MICRO      u8g2_font_3x5im_tr
#define FONT_CN         u8g2_font_wqy12_t_gb2312

MusicPlayerPage::MusicPlayerPage(std::vector<String> files, int startIndex)
    : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT) {
    fileList = files;
    currentIndex = (startIndex >= 0 && startIndex < (int)fileList.size()) ? startIndex : 0;
    songName = nameOf(fileList[currentIndex]);
    scrollOffset = 0;
    scrollTimer = millis();

    // 装载播放列表并起播(内部有界等待, 之后异步起播: UI永不阻塞)
    audioPlayer.loadPlaylist(fileList, currentIndex);
}

MusicPlayerPage::~MusicPlayerPage() {
    // 兜底停止(正常流程BACK已发停止命令); stop()是异步命令, 不阻塞析构
    if (!returning) {
        audioPlayer.stop();
    }
}

String MusicPlayerPage::nameOf(const String& path) {
    String n = path;
    int slash = n.lastIndexOf('/');
    if (slash >= 0) n = n.substring(slash + 1);
    int dot = n.lastIndexOf('.');
    if (dot > 0) n = n.substring(0, dot);
    return n;
}

void MusicPlayerPage::onTrackChanged() {
    songName = nameOf(fileList[currentIndex]);
    scrollOffset = 0;
    scrollTimer = millis();
    frozenTotalMs = 0;   // 新曲目重置时长兜底
}

// ================= 按键(按键任务直接执行; audioPlayer接口全部非阻塞入队, 无并发风险) =================
void MusicPlayerPage::togglePlayPause() {
    PlayerState st = audioPlayer.getState();
    if (st == PLAYER_PLAYING) {
        audioPlayer.pause();
        LOG_I("MPP[键] 暂停");
    } else if (st == PLAYER_PAUSED) {
        audioPlayer.resume();
        LOG_I("MPP[键] 恢复");
    } else if (st == PLAYER_IDLE) {
        // 播完停止后: 从当前曲重新播放(STARTING装载期间忽略防打断)
        audioPlayer.playIndex(currentIndex);
        LOG_I("MPP[键] 重新播放");
    }
}

void MusicPlayerPage::nextTrack() {
    int size = (int)fileList.size();
    if (size <= 0) return;
    currentIndex = (currentIndex + 1) % size;
    audioPlayer.playIndex(currentIndex);
    onTrackChanged();
    LOG_I("MPP[键] 下一曲 idx=%d", currentIndex);
}

void MusicPlayerPage::prevTrack() {
    int size = (int)fileList.size();
    if (size <= 0) return;
    currentIndex = (currentIndex - 1 + size) % size;
    audioPlayer.playIndex(currentIndex);
    onTrackChanged();
    LOG_I("MPP[键] 上一曲 idx=%d", currentIndex);
}

void MusicPlayerPage::cyclePlayMode() {
    PlayMode m = (PlayMode)(((int)audioPlayer.getPlayMode() + 1) % 4);
    audioPlayer.setPlayMode(m);
    flashMsg = modeNames[(int)m];
    flashUntil = millis() + 1200;
    LOG_I("MPP[键] 1 循环模式->%d", (int)m);
}

void MusicPlayerPage::cycleView() {
    viewMode = (ViewMode)(((int)viewMode + 1) % 4);
    flashMsg = viewNames[(int)viewMode];
    flashUntil = millis() + 900;
    LOG_I("MPP[视图] ->%d %s", (int)viewMode, viewNames[(int)viewMode]);
}

void MusicPlayerPage::requestReturn() {
    if (returning) return;
    returning = true;
    returnRequestedAt = millis();
    audioPlayer.stop();   // 异步命令(命令任务强制接管保证必停), UI不等待
}

// ---- 睡眠定时 ----
void MusicPlayerPage::openSleepMenu() {
    // 光标定位到当前值(自定义值定位到"自定义"项)
    int sel = 0;
    for (int i = 0; i < SLEEP_OPT_COUNT; i++) {
        if (sleepOptMinutes[i] == sleepMinutes) { sel = i; break; }
        if (sleepOptMinutes[i] < 0 && sleepMinutes > 0) sel = i;   // 预设外的自定义值
    }
    sleepSel = sel;
    sleepScroll = 0;
    if (sleepSel > sleepScroll + SLEEP_VISIBLE - 1) {
        sleepScroll = sleepSel - (SLEEP_VISIBLE - 1);
    }
    sleepMenuOpen = true;
    sleepInputOpen = false;
    sleepBuf = "";
    LOG_I("MPP[睡眠] 菜单打开");
}

void MusicPlayerPage::moveSleepSel(int delta) {
    sleepSel += delta;
    if (sleepSel < 0) sleepSel += SLEEP_OPT_COUNT;
    if (sleepSel >= SLEEP_OPT_COUNT) sleepSel -= SLEEP_OPT_COUNT;
    if (sleepSel < sleepScroll) sleepScroll = sleepSel;
    if (sleepSel >= sleepScroll + SLEEP_VISIBLE) sleepScroll = sleepSel - (SLEEP_VISIBLE - 1);
}

void MusicPlayerPage::sleepSelectCurrent() {
    if (sleepSel < 0 || sleepSel >= SLEEP_OPT_COUNT) return;
    if (sleepOptMinutes[sleepSel] < 0) {
        // 自定义: 预填当前自定义值
        sleepBuf = "";
        if (sleepMinutes > 0) {
            bool inPreset = false;
            for (int i = 0; i < SLEEP_OPT_COUNT; i++) {
                if (sleepOptMinutes[i] == sleepMinutes) { inPreset = true; break; }
            }
            if (!inPreset) sleepBuf = String(sleepMinutes);
        }
        sleepMenuOpen = false;
        sleepInputOpen = true;
        return;
    }
    applySleep(sleepOptMinutes[sleepSel]);
}

void MusicPlayerPage::acceptSleepInput() {
    if (sleepBuf.length() == 0) {
        // 空输入: 回到菜单
        sleepMenuOpen = true;
        sleepInputOpen = false;
        return;
    }
    int val = sleepBuf.toInt();
    if (val <= 0) val = 0;
    if (val > 600) val = 600;   // 上限600分钟(10小时)
    applySleep(val);
}

void MusicPlayerPage::appendSleepDigit(char d) {
    if (sleepBuf.length() >= 3) return;   // 最多3位
    sleepBuf += d;
}

void MusicPlayerPage::applySleep(int minutes) {
    sleepMinutes = minutes;
    sleepFired = false;
    sleepShutdownAt = 0;
    if (minutes > 0) {
        sleepDeadline = millis() + (unsigned long)minutes * 60000UL;
    } else {
        sleepDeadline = 0;
    }
    sleepMenuOpen = false;
    sleepInputOpen = false;
    sleepBuf = "";
    if (minutes > 0) {
        flashMsg = String("睡眠定时 ") + String(minutes) + "分";
    } else {
        flashMsg = "睡眠定时 已关闭";
    }
    flashUntil = millis() + 1200;
    LOG_I("MPP[睡眠] 定时=%d分", minutes);
}

void MusicPlayerPage::startSleepShutdown() {
    if (sleepFired) return;
    sleepFired = true;
    sleepShutdownAt = millis() + 1500;   // 先提示1.5秒再深度休眠
    audioPlayer.stop();
    LOG_I("MPP[睡眠] 到点, 已停止播放, 即将休眠");
}

void MusicPlayerPage::onButtonBack(void* context) {
    if (sleepShutdownAt) return;
    if (sleepInputOpen) {
        if (sleepBuf.length() > 0) {
            sleepBuf = sleepBuf.substring(0, sleepBuf.length() - 1);   // 删除一位数字
        } else {
            sleepMenuOpen = true;   // 返回菜单
            sleepInputOpen = false;
        }
        return;
    }
    if (sleepMenuOpen) {
        sleepMenuOpen = false;
        return;
    }
    LOG_I("MPP[键] BACK(停止返回)");
    requestReturn();
}

void MusicPlayerPage::onButtonMenu(void* context) {
    if (sleepShutdownAt) return;
    if (sleepMenuOpen || sleepInputOpen) {
        sleepMenuOpen = false;
        sleepInputOpen = false;
        sleepBuf = "";
        return;
    }
    openSleepMenu();
}

void MusicPlayerPage::onButtonEnter(void* context) {
    if (sleepShutdownAt) return;
    if (sleepMenuOpen) { sleepSelectCurrent(); return; }
    if (sleepInputOpen) { acceptSleepInput(); return; }
    togglePlayPause();
}

void MusicPlayerPage::onButton1(void* context) {
    if (sleepShutdownAt) return;
    if (sleepInputOpen) { appendSleepDigit('1'); return; }
    if (sleepMenuOpen) return;
    cyclePlayMode();
}

void MusicPlayerPage::onButton2(void* context) {
    if (sleepShutdownAt) return;
    if (sleepInputOpen) { appendSleepDigit('2'); return; }
    if (sleepMenuOpen) { moveSleepSel(-1); return; }
    audioPlayer.setVolume(audioPlayer.getVolume() + 10);   // 2: 音量+ (上键)
}

void MusicPlayerPage::onButton3(void* context) {
    if (sleepShutdownAt) return;
    if (sleepInputOpen) { appendSleepDigit('3'); return; }
    if (sleepMenuOpen) return;
    cycleView();
}

void MusicPlayerPage::onButton4(void* context) {
    if (sleepShutdownAt) return;
    if (sleepMenuOpen || sleepInputOpen) return;
    prevTrack();      // 4: 上一曲 (左键)
}

void MusicPlayerPage::onButton5(void* context) {
    if (sleepShutdownAt) return;
    if (sleepMenuOpen) { sleepSelectCurrent(); return; }
    if (sleepInputOpen) { appendSleepDigit('5'); return; }   // 输入状态5作数字键, ENTER确定
    togglePlayPause();    // 5: 播放/暂停
}

void MusicPlayerPage::onButton6(void* context) {
    if (sleepShutdownAt) return;
    if (sleepMenuOpen || sleepInputOpen) return;
    nextTrack();      // 6: 下一曲 (右键)
}

void MusicPlayerPage::onButton7(void* context) {
    if (sleepShutdownAt) return;
    if (sleepInputOpen) { appendSleepDigit('7'); }
}

void MusicPlayerPage::onButton8(void* context) {
    if (sleepShutdownAt) return;
    if (sleepInputOpen) { appendSleepDigit('8'); return; }
    if (sleepMenuOpen) { moveSleepSel(1); return; }
    audioPlayer.setVolume(audioPlayer.getVolume() - 10);   // 8: 音量- (下键)
}

void MusicPlayerPage::onButton9(void* context) {
    if (sleepShutdownAt) return;
    if (sleepInputOpen) { appendSleepDigit('9'); }
}

// ================= 帧更新(渲染任务: 仅帧驱动逻辑, 按键已在onButtonXxx直接执行) =================
void MusicPlayerPage::update() {
    // 睡眠倒计时到点(不随按键重置)
    if (sleepMinutes > 0 && !sleepFired && sleepDeadline != 0 &&
        (long)(millis() - sleepDeadline) >= 0) {
        startSleepShutdown();
    }
    // 提示延时结束: 进入深度休眠(不返回)
    if (sleepShutdownAt != 0 && (long)(millis() - sleepShutdownAt) >= 0) {
        LOG_I("MPP[睡眠] 进入深度休眠");
        systemSetting.enterSleep();
        sleepShutdownAt = 0;
        return;
    }

    // 菜单/输入打开时: 冻结播放页其余帧逻辑(音频继续)
    if (sleepMenuOpen || sleepInputOpen) return;

    // 返回流程: 等音频任务停止(空闲)后导航(防止与浏览器页文件访问并发), 2秒兜底
    if (returning) {
        if (audioPlayer.getState() == PLAYER_IDLE || millis() - returnRequestedAt > 2000) {
            uiEngine.navigateBack();
        }
        return;
    }

    // 启动失败(整个列表): 提示1.2秒后自动返回
    if (audioPlayer.isStartFailed()) {
        if (startFailedAt == 0) startFailedAt = millis();
        if (millis() - startFailedAt > 1200) {
            requestReturn();
        }
        return;
    }

    // 曲目切换同步(自动连播/随机模式由音频任务驱动)
    PlayerState st = audioPlayer.getState();
    if (st != PLAYER_IDLE) sawPlayback = true;   // 观测到起播过
    int idx = audioPlayer.getIndex();
    if (idx >= 0 && idx < (int)fileList.size() && idx != currentIndex) {
        currentIndex = idx;
        onTrackChanged();
    }

    // 视图相关刷新(节流在各自函数内; 非PLAYING时衰减到零)
    if (viewMode == VIEW_SPECTRUM) {
        updateSpectrum();
    } else if (viewMode == VIEW_WAVEFORM) {
        updateWaveform();
    } else if (viewMode == VIEW_ANALOG_VU) {
        // 指针平滑: 一阶低通, 每帧更新
        float targetL = (float)audioPlayer.getVuL();
        float targetR = (float)audioPlayer.getVuR();
        if (st != PLAYER_PLAYING) { targetL = 0; targetR = 0; }
        vuSmoothL = vuSmoothL * 0.65f + targetL * 0.35f;
        vuSmoothR = vuSmoothR * 0.65f + targetR * 0.35f;
        if (vuSmoothL < 0.5f) vuSmoothL = 0;
        if (vuSmoothR < 0.5f) vuSmoothR = 0;
    }

    // 歌名滚动(仅默认视图需要, 但保持计算无害)
    updateScroll();

    // 电池电量(周期刷新)
    if (millis() - lastBatteryCheck > 2000) {
        lastBatteryCheck = millis();
        batteryLevel = voltageToLevel(batteryManager.getLastVoltage());
    }
}

// ================= 频谱: FFT + 12带映射 =================
void MusicPlayerPage::updateSpectrum() {
    const unsigned long now = millis();
    if (now - lastSpecMs < 45) return; // ~22fps, 1024点FFT约8-10ms
    lastSpecMs = now;

    PlayerState st = audioPlayer.getState();
    if (st != PLAYER_PLAYING) {
        for (int i = 0; i < 12; i++) {
            if (specBands[i] > 4) specBands[i] -= 4;
            else specBands[i] = 0;
            if (specPeak[i] > 1) specPeak[i] -= 1;
            else specPeak[i] = 0;
        }
        return;
    }

    static const uint16_t N = 1024;
    static int16_t mono[N];
    if (!audioPlayer.getSpectrumSamples(mono, N)) {
        return;
    }
    static float vReal[N];
    static float vImag[N];
    float sampleRate = (float)audioPlayer.getSampleRate();
    if (sampleRate < 8000) sampleRate = 44100;

    // 去直流(避免0Hz泄漏抬高50Hz带)
    int32_t sum = 0;
    for (int i = 0; i < (int)N; i++) sum += mono[i];
    float dc = (float)sum / (float)N;
    for (int i = 0; i < (int)N; i++) {
        vReal[i] = (float)mono[i] - dc;
        vImag[i] = 0;
    }
    ArduinoFFT<float> FFT(vReal, vImag, N, sampleRate);
    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT.compute(FFTDirection::Forward);
    FFT.complexToMagnitude();

    // 12带按固定边界分bin, 带内取峰值(最大值) — 均值会把宽带能量抹平导致12条同高
    // N=1024@44.1k bin宽43Hz: 50Hz带含bin1(43Hz), 不再为空
    const float edges[13] = {0,75,130,205,375,625,875,1500,3000,6000,12000,18000, 50000};
    float bandPeak[12] = {0};
    for (int i = 1; i < (int)(N/2); i++) {
        float freq = (float)i * sampleRate / (float)N;
        int b = -1;
        for (int k = 0; k < 12; k++) {
            if (freq >= edges[k] && freq < edges[k+1]) { b = k; break; }
        }
        if (b < 0) continue;
        if (vReal[i] > bandPeak[b]) bandPeak[b] = vReal[i];
    }
    // 正确归一化: 满幅正弦32767 -> mag ≈ 32767*N/2*0.54 (Hamming窗相干增益0.54)
    // 上一版 divisor 漏了 *32768，导致所有带 20*log10(mag/(N/2*0.54)) 普遍>40dB而顶满；
    // 再用均值 bandSum/bandCnt 则把宽带谱抹平成全带同高 — 两错叠加
    const float windowGain = 0.54f;
    const float divisor = (float)N * 0.5f * windowGain * 32768.0f;
    for (int b = 0; b < 12; b++) {
        float pkMag = bandPeak[b];
        uint8_t raw = 0;
        if (pkMag > 1.0f) {
            float dbFs = 20.0f * log10f(pkMag / divisor + 1e-12f);
            float pct = (dbFs + 60.0f) / 60.0f * 100.0f;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            raw = (uint8_t)pct;
        }
        if (raw > specBands[b]) {
            specBands[b] = (uint8_t)(specBands[b] * 0.25f + raw * 0.75f);
        } else {
            if (specBands[b] > 3) specBands[b] -= 3;
            else if (specBands[b] > 0) specBands[b]--;
            if (raw > specBands[b]) specBands[b] = raw;
        }
        if (raw > specPeak[b]) specPeak[b] = raw;
        else if (specPeak[b] > 0) specPeak[b]--;
    }
}

void MusicPlayerPage::updateWaveform() {
    const unsigned long now = millis();
    if (now - lastWaveMs < 32) return; // ~30fps
    lastWaveMs = now;

    if (audioPlayer.getState() != PLAYER_PLAYING) {
        // 非播放时拉平到0(基线)
        for (int i = 0; i < 128; i++) { waveL[i] = 0; waveR[i] = 0; }
        return;
    }
    const int SRC = 512;
    int16_t tmpL[SRC], tmpR[SRC];
    if (!audioPlayer.getWaveformSamples(tmpL, tmpR, SRC)) return;
    // 512 -> 128 平均降采样
    for (int i = 0; i < 128; i++) {
        int32_t sL = 0, sR = 0;
        for (int k = 0; k < 4; k++) { sL += tmpL[i*4 + k]; sR += tmpR[i*4 + k]; }
        waveL[i] = (int16_t)(sL / 4);
        waveR[i] = (int16_t)(sR / 4);
    }
}

void MusicPlayerPage::updateScroll() {
    const int viewW = 118;
    if (songNameWidth <= viewW) return;

    const unsigned long now = millis();
    if (now - scrollTimer <= 35) {
        return;
    }
    scrollTimer = now;

    // 连续无缝滚动: 视图里始终放两份相同文本，第二份紧跟第一份尾部。
    // 当第一份完全离开左边后，重置偏移回到一个周期内，视觉上无缝衔接。
    scrollOffset += 2;
    const int gap = 16;
    const int cycle = songNameWidth + gap;
    if (scrollOffset >= cycle) {
        scrollOffset -= cycle;
    }
}

// ================= 绘制(渲染任务调用) =================
void MusicPlayerPage::render(U8G2* u8g2) {
    if (audioPlayer.isStartFailed()) {
        u8g2->setFont(FONT_CN);
        const char* msg = "无法播放该文件";
        uint16_t w = u8g2->getUTF8Width(msg);
        u8g2->drawUTF8((SCREEN_WIDTH - w) / 2, 32, msg);
        return;
    }

    if (returning) {
        u8g2->setFont(FONT_CN);
        const char* msg = "停止中...";
        uint16_t w = u8g2->getUTF8Width(msg);
        u8g2->drawUTF8((SCREEN_WIDTH - w) / 2, 32, msg);
        return;
    }

    // 睡眠到点提示
    if (sleepShutdownAt) {
        u8g2->setFont(FONT_CN);
        const char* l1 = "睡眠定时到点";
        uint16_t w1 = u8g2->getUTF8Width(l1);
        u8g2->drawUTF8((SCREEN_WIDTH - w1) / 2, 28, l1);
        const char* l2 = "即将进入休眠...";
        uint16_t w2 = u8g2->getUTF8Width(l2);
        u8g2->drawUTF8((SCREEN_WIDTH - w2) / 2, 46, l2);
        return;
    }

    // 睡眠菜单/自定义输入
    if (sleepMenuOpen) { drawSleepMenuUI(u8g2); return; }
    if (sleepInputOpen) { drawSleepInputUI(u8g2); return; }

    drawStatusBar(u8g2);
    u8g2->drawHLine(0, 11, SCREEN_WIDTH);

    switch (viewMode) {
        case VIEW_SPECTRUM:
            drawSpectrumView(u8g2);
            break;
        case VIEW_ANALOG_VU:
            drawAnalogVuView(u8g2);
            break;
        case VIEW_WAVEFORM:
            drawWaveformView(u8g2);
            break;
        case VIEW_DEFAULT:
        default: {
            if (audioPlayer.getState() == PLAYER_STARTING || !sawPlayback) {
                u8g2->setFont(FONT_CN);
                const char* loading = "加载中...";
                uint16_t lw = u8g2->getUTF8Width(loading);
                u8g2->drawUTF8((SCREEN_WIDTH - lw) / 2, 23, loading);
            } else {
                drawSongTitle(u8g2);
                drawInfoLine(u8g2);
            }
            if (!(millis() < flashUntil && flashMsg.length() > 0)) {
                drawVUMeter(u8g2);
            }
            drawPlaybackRow(u8g2);
            break;
        }
    }
    // 视图切换短暂提示(覆盖在内容区顶部居中, 不遮状态栏)
    if (viewMode != VIEW_DEFAULT && millis() < flashUntil && flashMsg.length() > 0) {
        u8g2->setFont(FONT_CN);
        int tw = u8g2->getUTF8Width(flashMsg.c_str());
        int bw = tw + 6;
        int bx = (SCREEN_WIDTH - bw) / 2;
        if (bx < 0) bx = 0;
        if (bx + bw > SCREEN_WIDTH) bw = SCREEN_WIDTH - bx;
        u8g2->setDrawColor(0);
        u8g2->drawBox(bx, 12, bw, 14);
        u8g2->setDrawColor(1);
        u8g2->drawFrame(bx, 12, bw, 14);
        u8g2->drawUTF8(bx + 3, 23, flashMsg.c_str());
    }
}

void MusicPlayerPage::drawStatusBar(U8G2* u8g2) {
    // 循环模式图标
    u8g2->drawXBM(ICON_MODE_X, 0, 10, 10, modeIcons[(int)audioPlayer.getPlayMode()]);

    // 睡眠定时开启: 模式与音量之间放时钟图标(Siji 57366), 音量整体右移
    if (sleepMinutes > 0 && !sleepFired) {
        u8g2->setFont(u8g2_font_siji_t_6x10);
        u8g2->drawGlyph(ICON_SLEEP_X, 9, GLYPH_CLOCK);
    }

    // 音量: 喇叭图标(waffle字体) + 百分比数字
    u8g2->setFont(u8g2_font_waffle_t_all);
    u8g2->drawGlyph(ICON_SPK_X, 9, audioPlayer.getVolume() > 0 ? 57859 : 57858);
    u8g2->setFont(FONT_SMALL);
    char volStr[6];
    snprintf(volStr, sizeof(volStr), "%d%%", audioPlayer.getVolume());
    u8g2->drawStr(VOLBAR_X, 9, volStr);

    // 曲目序号 (如 "3/12", 右边界104避开电池)
    String trackText = String(currentIndex + 1) + "/" + String(fileList.size());
    int tw = u8g2->getStrWidth(trackText.c_str());
    u8g2->drawStr(TRACK_RIGHT - tw, 9, trackText.c_str());

    // 电池(与首页状态栏同款)
    u8g2->setFont(u8g2_font_battery19_tn);
    u8g2->setFontDirection(3);
    u8g2->drawGlyph(SCREEN_WIDTH, 8, 48 + batteryLevel);
    u8g2->setFontDirection(0);
}

void MusicPlayerPage::drawSongTitle(U8G2* u8g2) {
    u8g2->setFont(FONT_CN);
    const char* name = songName.c_str();
    songNameWidth = u8g2->getUTF8Width(name);

    const int viewW = 118;
    if (songNameWidth <= viewW) {
        u8g2->drawUTF8((SCREEN_WIDTH - songNameWidth) / 2, 23, name);
    } else {
        const int gap = 16;
        const int cycle = songNameWidth + gap;
        int x = 5 - scrollOffset;

        u8g2->setClipWindow(5, 12, 122, 25);
        u8g2->drawUTF8(x, 23, name);
        u8g2->drawUTF8(x + cycle, 23, name);
        u8g2->setMaxClipWindow();
    }
}

void MusicPlayerPage::drawInfoLine(U8G2* u8g2) {
    // 循环模式/睡眠设置后的短暂提示(信息区整块居中)
    if (millis() < flashUntil && flashMsg.length() > 0) {
        u8g2->setFont(FONT_CN);
        uint16_t w = u8g2->getUTF8Width(flashMsg.c_str());
        u8g2->drawUTF8((SCREEN_WIDTH - w) / 2, 40, flashMsg.c_str());
        return;
    }

    // 左侧2行(右半侧让给电平表): 第一行 格式+大小 | 第二行 采样率+位深
    u8g2->setFont(FONT_TINY);

    String path = fileList[currentIndex];
    String lower = path;
    lower.toLowerCase();
    String fmt;
    if (lower.endsWith(".mp3"))  fmt = "MP3";
    else if (lower.endsWith(".wav"))  fmt = "WAV";
    else if (lower.endsWith(".flac")) fmt = "FLAC";
    else if (lower.endsWith(".aac"))  fmt = "AAC";
    else if (lower.endsWith(".opus") || lower.endsWith(".ogg")) fmt = "OGG";
    else if (lower.endsWith(".mid") || lower.endsWith(".midi")) fmt = "MIDI";
    else if (lower.endsWith(".mod"))  fmt = "MOD";
    else fmt = "AUDIO";

    uint32_t fsize = audioPlayer.getFileSize();
    char sizeStr[10];
    if (fsize >= 100 * 1024 * 1024UL) snprintf(sizeStr, sizeof(sizeStr), "%luMB", (unsigned long)(fsize / 1048576));
    else if (fsize >= 1024 * 1024) snprintf(sizeStr, sizeof(sizeStr), "%.1fMB", fsize / 1048576.0);
    else if (fsize >= 1024) snprintf(sizeStr, sizeof(sizeStr), "%luKB", (unsigned long)(fsize / 1024));
    else strcpy(sizeStr, "");

    // 第一行: 格式(左) + 大小(紧跟格式后, 不再右对齐拉出大间隔)
    int fw = u8g2->getStrWidth(fmt.c_str());
    u8g2->drawUTF8(2, 33, fmt.c_str());
    if (sizeStr[0]) {
        u8g2->drawUTF8(2 + fw + 8, 33, sizeStr);
    }

    // 第二行: 采样率 + 位深(完整bit)
    uint32_t rate = audioPlayer.getSampleRate();
    char rateStr[10];
    if (rate >= 100000) snprintf(rateStr, sizeof(rateStr), "%luk", (unsigned long)(rate / 1000));
    else if (rate >= 1000) snprintf(rateStr, sizeof(rateStr), "%.1fk", rate / 1000.0);
    else if (rate > 0) snprintf(rateStr, sizeof(rateStr), "%lu", (unsigned long)rate);
    else strcpy(rateStr, "");

    uint16_t bits = audioPlayer.getBitDepth();
    char midStr[16];
    if (rateStr[0]) {
        snprintf(midStr, sizeof(midStr), "%s %ubit", rateStr, (unsigned)bits);
    } else if (bits > 0) {
        snprintf(midStr, sizeof(midStr), "%ubit", (unsigned)bits);
    } else {
        midStr[0] = '\0';   // 停止后无采样率无位深: 不显示
    }
    u8g2->drawUTF8(2, 43, midStr);
}

// ================= VU电平表(右半侧2行: 上行L/下行R) =================
// SONY D100风格: 无背景, 电平为多根1px宽竖线(间隔1px), 峰值保持为2px宽竖线以示区分
static void drawVuRow(U8G2* u8g2, int x, int y, int w, int h, int level, int peak) {
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    if (peak < 0) peak = 0;
    if (peak > 100) peak = 100;

    // 电平: 1px竖线 + 1px间隔, 从左亮起
    const int segments = w / 2;              // 可显示的最大竖线根数
    int lit = (segments * level) / 100;
    for (int i = 0; i < lit; i++) {
        u8g2->drawVLine(x + i * 2, y, h);    // 1px宽
    }

    // 峰值保持: 2px宽竖线(压在最后位置, 与1px电平线明显区分); 全静音时峰值归零则不画
    if (peak > 0) {
        int pkX = x + (segments * peak) / 100 * 2;
        if (pkX > x + w - 2) pkX = x + w - 2;
        u8g2->drawBox(pkX, y, 2, h);
    }
}

void MusicPlayerPage::drawVUMeter(U8G2* u8g2) {
    u8g2->setFont(FONT_TINY);
    u8g2->drawStr(65, 33, "L");
    u8g2->drawStr(65, 43, "R");
    // 相比信息文字上移1px(原y28/y39), L/R各自独立
    drawVuRow(u8g2, 72, 27, 54, 7, audioPlayer.getVuL(), audioPlayer.getPeakL());
    drawVuRow(u8g2, 72, 38, 54, 7, audioPlayer.getVuR(), audioPlayer.getPeakR());
}

// ================= 频谱视图 =================
void MusicPlayerPage::drawSpectrumView(U8G2* u8g2) {
    const int barBottom = 55;
    const int barTop = 14;
    const int barH = barBottom - barTop + 1; // 42
    const int levelsMax = barH / 2; // 21 levels (1px line +1px gap)
    // 12段: x=4+i*10, w=9, gap=1
    for (int i = 0; i < 12; i++) {
        int x = 4 + i * 10;
        int pct = specBands[i];
        int peak = specPeak[i];
        if (pct < 0) pct = 0; if (pct > 100) pct = 100;
        if (peak < 0) peak = 0; if (peak > 100) peak = 100;
        int lit = (levelsMax * pct) / 100;
        for (int lv = 0; lv < lit; lv++) {
            int y = barBottom - lv * 2;
            u8g2->drawHLine(x, y, 9);
        }
        if (peak > 0) {
            int pkLv = (levelsMax * peak) / 100;
            int py = barBottom - pkLv * 2;
            if (py < barTop) py = barTop;
            // 2px高的峰值线, 与1px条区分
            u8g2->drawBox(x, py - 1, 9, 2);
        }
    }
    // 底部标签，间距2像素平铺开来，不用对齐上方的bar
    u8g2->setFont(FONT_MICRO);
    int lx = 1;
    for (int i = 0; i < 12; i++) {
        const char* lb = specLabels[i];
        int sw = u8g2->getStrWidth(lb);
        // int centerX = ((2 * i + 1) * 128) / 24; // 5,16,27,37,48,59,69,80,91,101,112,122
        // int lx = centerX - sw / 2;
        // if (lx < 0) lx = 0;
        // if (lx + sw > 128) lx = 128 - sw;
        u8g2->drawStr(lx, 63, lb);
        lx += sw + 2;
    }
}

// ================= 指针VU视图 =================
void MusicPlayerPage::drawAnalogMeter(U8G2* u8g2, int cx, int cy, int r, int pct, int peak, const char* label) {
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    if (peak < 0) peak = 0; if (peak > 100) peak = 100;

    // 像素级对称: 先算右半弧再镜像到左半, 每像素都成对出现, 浮点截断造成的左右1px错位彻底消除
    // 90°扇形(-135°~-45°, 中心-90°) 拆32段, 步进2.8125°
    const int halfSegs = 16;
    const float step = 90.0f / (halfSegs * 2); // 2.8125°
    int rx[33], ry[33];
    for (int i = 0; i <= halfSegs; i++) {
        float a = -90.0f + step * i; // 0..+45°
        float rad = a * (float)M_PI / 180.0f;
        int dx = (int)lroundf(cosf(rad) * r);
        int dy = (int)lroundf(sinf(rad) * r);
        rx[halfSegs + i] = cx + dx;
        ry[halfSegs + i] = cy + dy;
        rx[halfSegs - i] = cx - dx;
        ry[halfSegs - i] = cy + dy;
    }
    for (int i = 0; i < halfSegs * 2; i++) {
        u8g2->drawLine(rx[i], ry[i], rx[i+1], ry[i+1]);
    }

    // 刻度极简化: 5根主刻度等角距(-135,-112.5,-90,-67.5,-45)严格对称, 无次刻度
    // 4px长(右半镜像左半), 中心一根稍长5px作视觉锚点, 干净不糊
    const float tickAngles[5] = {-135.0f, -112.5f, -90.0f, -67.5f, -45.0f};
    for (int i = 0; i < 5; i++) {
        float rad = tickAngles[i] * (float)M_PI / 180.0f;
        int len = (i == 2) ? 5 : 4;
        int x0 = cx + (int)lroundf(cosf(rad) * r);
        int y0 = cy + (int)lroundf(sinf(rad) * r);
        int x1 = cx + (int)lroundf(cosf(rad) * (r - len));
        int y1 = cy + (int)lroundf(sinf(rad) * (r - len));
        u8g2->drawLine(x0, y0, x1, y1);
    }

    // 指针 (round保证左右摆幅像素对称)
    float ang = (-135.0f + 90.0f * pct / 100.0f) * (float)M_PI / 180.0f;
    int needleLen = r - 4;
    int x1 = cx + (int)lroundf(cosf(ang) * needleLen);
    int y1 = cy + (int)lroundf(sinf(ang) * needleLen);
    u8g2->drawLine(cx, cy, x1, y1);
    u8g2->drawDisc(cx, cy, 3, U8G2_DRAW_ALL);
    u8g2->setDrawColor(0);
    u8g2->drawDisc(cx, cy, 1, U8G2_DRAW_ALL);
    u8g2->setDrawColor(1);

    // 峰值点(弧外小方块)
    if (peak > 5) {
        float pa = (-135.0f + 90.0f * peak / 100.0f) * (float)M_PI / 180.0f;
        int px = cx + (int)lroundf(cosf(pa) * (r + 3));
        int py = cy + (int)lroundf(sinf(pa) * (r + 3));
        u8g2->drawBox(px - 1, py - 1, 3, 3);
    }

    // 底部: VU居中, L/R在左下角, dB在右下角
    u8g2->setFont(FONT_TINY);
    const char* vu = "VU";
    int sw = u8g2->getStrWidth(vu);
    u8g2->drawStr(cx - sw/2, 63, vu);

    char dbStr[10];
    float db = -20.0f + pct * 0.23f; // 0->-20, 85->0, 100->+3
    if (pct < 2) strcpy(dbStr, "-inf");
    else snprintf(dbStr, sizeof(dbStr), "%.0fdB", db);
    u8g2->setFont(FONT_TINY);
    u8g2->drawStr(cx - r + 2, 63, label);
    int dbW = u8g2->getStrWidth(dbStr);
    u8g2->drawStr(cx + r - dbW - 2, 63, dbStr);
}

void MusicPlayerPage::drawAnalogVuView(U8G2* u8g2) {
    for (int y = 13; y <= 63; y += 4) {
        u8g2->drawPixel(64, y);
    }
    int pctL = (int)vuSmoothL;
    int pctR = (int)vuSmoothR;
    int peakL = audioPlayer.getPeakL();
    int peakR = audioPlayer.getPeakR();
    // 下移+放大: cy=46 r=30, 顶部y=16贴近状态栏下方, 底部VU/dB在y=63, 表盘最大化
    drawAnalogMeter(u8g2, 32, 46, 30, pctL, peakL, "L");
    drawAnalogMeter(u8g2, 96, 46, 30, pctR, peakR, "R");
}

// ================= 波形视图 =================
void MusicPlayerPage::drawWaveformView(U8G2* u8g2) {
    // 极简: 仅一条横分割线分隔上下 L/R, 无边框无网格, 干净专业
    const int divY = 37;          // 水平分割线 y
    const int midL = 24;          // L 中心 y (12..37 区间中心)
    const int midR = 51;          // R 中心 y (38..64 区间中心)
    const int hHalf = 10;         // 半高(留1px顶底余量)

    // 中间分割线
    u8g2->drawHLine(0, divY, 128);

    // 标签 L/R (左上角, 4x6)
    u8g2->setFont(FONT_TINY);
    u8g2->drawStr(2, 20, "L");
    u8g2->drawStr(2, 46, "R");

    // 波形连线(限幅到内容区, 不越过状态栏/底部)
    const int topL = 13, botL = 36;
    const int topR = 38, botR = 63;
    int prevYL = midL - (waveL[0] * hHalf / 32768);
    int prevYR = midR - (waveR[0] * hHalf / 32768);
    if (prevYL < topL) prevYL = topL; if (prevYL > botL) prevYL = botL;
    if (prevYR < topR) prevYR = topR; if (prevYR > botR) prevYR = botR;
    bool clipL = false, clipR = false;
    for (int x = 1; x < 128; x++) {
        int yL = midL - (waveL[x] * hHalf / 32768);
        int yR = midR - (waveR[x] * hHalf / 32768);
        if (yL < topL) yL = topL; if (yL > botL) yL = botL;
        if (yR < topR) yR = topR; if (yR > botR) yR = botR;
        u8g2->drawLine(x-1, prevYL, x, yL);
        u8g2->drawLine(x-1, prevYR, x, yR);
        prevYL = yL; prevYR = yR;
        if (abs((int)waveL[x]) >= 32600) clipL = true;
        if (abs((int)waveR[x]) >= 32600) clipR = true;
    }
    // 削波提示(保留: 削波时右上角小 "!")
    if (clipL) {
        u8g2->setDrawColor(1);
        u8g2->drawBox(120, 13, 7, 7);
        u8g2->setDrawColor(0);
        u8g2->setFont(FONT_TINY);
        u8g2->drawStr(121, 19, "!");
        u8g2->setDrawColor(1);
    }
    if (clipR) {
        u8g2->setDrawColor(1);
        u8g2->drawBox(120, 39, 7, 7);
        u8g2->setDrawColor(0);
        u8g2->setFont(FONT_TINY);
        u8g2->drawStr(121, 45, "!");
        u8g2->setDrawColor(1);
    }
}

// 播放状态图标(左) + 时间(右, 底部行) + 进度条(其上)
void MusicPlayerPage::drawPlaybackRow(U8G2* u8g2) {
    // 快照读取(音频任务更新, 免锁)
    uint32_t total = audioPlayer.getTotalMs();
    uint32_t elapsed = audioPlayer.getElapsedMs();
    uint32_t fpos = audioPlayer.getFilePos();
    uint32_t fsz = audioPlayer.getFileSize();

    if (total == 0) {
        // 兜底: 头部解析不出总时长时, 播放5秒后一次性推算并冻结
        if (frozenTotalMs == 0 && audioPlayer.isPlaying() &&
            elapsed > 5000 && fpos > 8192 && fsz > 0) {
            frozenTotalMs = (uint32_t)((uint64_t)elapsed * fsz / fpos);
        }
        total = frozenTotalMs;
    }

    // ---- 进度条(播放状态/时间行上方) ----
    float ratio;
    if (total > 0) {
        ratio = (float)elapsed / (float)total;
    } else {
        ratio = (fsz > 0) ? (float)fpos / (float)fsz : 0.0f;
    }
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;

    const int barX = 2, barY = 48, barW = 124, barH = 6;
    int innerW = barW - 4;
    int fillW = (int)(innerW * ratio);

    u8g2->drawFrame(barX, barY, barW, barH);
    if (fillW > 0) {
        u8g2->drawBox(barX + 2, barY + 2, fillW, 2);
    }
    int headX = barX + 2 + fillW;
    if (headX > barX + barW - 3) headX = barX + barW - 3;
    u8g2->drawBox(headX, barY + 1, 2, 4);

    // ---- 时间: 当前时间(最左) + 总时长(最右) ----
    u8g2->setFont(FONT_SMALL);
    char ebuf[8], tbuf[8];
    uint32_t es = elapsed / 1000, et = total / 1000;
    snprintf(ebuf, sizeof(ebuf), "%02lu:%02lu", (unsigned long)(es / 60), (unsigned long)(es % 60));
    if (total > 0) {
        snprintf(tbuf, sizeof(tbuf), "%02lu:%02lu", (unsigned long)(et / 60), (unsigned long)(et % 60));
    } else {
        strcpy(tbuf, "--:--");
    }
    u8g2->drawStr(2, 62, ebuf);
    int tw = u8g2->getStrWidth(tbuf);
    u8g2->drawStr(SCREEN_WIDTH - 2 - tw, 62, tbuf);

    // ---- 播放/暂停图标(Siji 57498/57499, 居中) ----
    bool playing = (audioPlayer.getState() == PLAYER_PLAYING);
    uint16_t iconCode = playing ? GLYPH_PAUSE : GLYPH_PLAY;
    u8g2->setFont(u8g2_font_siji_t_6x10);
    int gw = u8g2_GetGlyphWidth(u8g2->getU8g2(), iconCode);
    if (gw < 1) gw = 7;
    u8g2->drawGlyph((SCREEN_WIDTH - gw) / 2, 62, iconCode);
}

// ================= 睡眠菜单绘制 =================
void MusicPlayerPage::drawSleepMenuUI(U8G2* u8g2) {
    // 标题
    u8g2->setFont(FONT_CN);
    const char* t = "睡眠定时";
    uint16_t tw = u8g2->getUTF8Width(t);
    u8g2->drawUTF8((SCREEN_WIDTH - tw) / 2, 11, t);
    u8g2->drawHLine(0, 13, SCREEN_WIDTH);

    // 选项列表(一屏SLEEP_VISIBLE行)
    for (int i = 0; i < SLEEP_VISIBLE; i++) {
        int idx = sleepScroll + i;
        if (idx >= SLEEP_OPT_COUNT) break;
        int y = 14 + i * 12;
        bool selected = (idx == sleepSel);
        if (selected) {
            u8g2->setDrawColor(1);
            u8g2->drawBox(0, y, SCREEN_WIDTH, 12);
        }
        // 反色用drawColor 0/1 + 透明字体模式; 不能用XOR(实心模式+XOR会整格变黑块)
        u8g2->setFontMode(1);
        u8g2->setDrawColor(selected ? 0 : 1);
        u8g2->setFont(FONT_CN);
        u8g2->drawUTF8(6, y + 10, sleepLabels[idx]);
        u8g2->setDrawColor(1);
    }
    u8g2->setFontMode(0);
}

void MusicPlayerPage::drawSleepInputUI(U8G2* u8g2) {
    u8g2->setFont(FONT_CN);
    const char* t = "自定义睡眠分钟";
    uint16_t tw = u8g2->getUTF8Width(t);
    u8g2->drawUTF8((SCREEN_WIDTH - tw) / 2, 11, t);
    u8g2->drawHLine(0, 13, SCREEN_WIDTH);

    // 当前输入值(未输入则提示)
    String v = sleepBuf.length() > 0 ? (sleepBuf + "分") : String("请输入分钟数");
    u8g2->setFont(FONT_CN);
    uint16_t vw = u8g2->getUTF8Width(v.c_str());
    u8g2->drawUTF8((SCREEN_WIDTH - vw) / 2, 32, v.c_str());

    // 提示
    u8g2->setFont(FONT_CN);
    const char* h1 = "1-9键输入分钟数";
    u8g2->drawUTF8((SCREEN_WIDTH - u8g2->getUTF8Width(h1)) / 2, 46, h1);
    const char* h2 = "ENTER确定  BACK删除";
    u8g2->drawUTF8((SCREEN_WIDTH - u8g2->getUTF8Width(h2)) / 2, 58, h2);
}

// ================= 工具 =================
String MusicPlayerPage::formatTimeMs(uint32_t ms) {
    uint32_t s = ms / 1000;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    return String(buf);
}

int MusicPlayerPage::voltageToLevel(float voltage) {
    if (voltage >= 4.0) return 5;
    if (voltage >= 3.8) return 4;
    if (voltage >= 3.6) return 3;
    if (voltage >= 3.4) return 2;
    if (voltage >= 3.2) return 1;
    return 0;
}
