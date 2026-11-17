/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// RecordPage.cpp
// 按键: 2=增益+ 8=增益- 5/ENTER=开始/暂停/继续 BACK=停止录音并返回
#include "RecordPage.h"
#include "../../GUI/UIEngine.h"
#include "../../Drivers/BatteryManager.h"
#include "../../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "REC"

extern UIEngine uiEngine;
extern BatteryManager batteryManager;

#define FONT_SMALL      u8g2_font_profont11_tf
#define FONT_TINY       u8g2_font_4x6_tf
#define FONT_CN         u8g2_font_wqy12_t_gb2312
#define FONT_BIG        u8g2_font_helvB14_tr    // REC/时间大字(与启动画面同款)
#define FONT_LABEL      u8g2_font_5x7_tf        // 底部文件名

// 电平区布局: L/R标签 | 电平条 | 爆VU框 | 增益(右对齐)
#define RP_BAR_X        6     // 电平条起点x(左侧留L/R标签)
#define RP_BAR_W        84    // 电平条宽(42根1px竖线)
#define RP_BAR_H        5     // 电平条高(L/R各5px, 两行间留2px防视觉粘连)
#define RP_VU_L_Y       1     // L电平条顶y(墨迹y1-5)
#define RP_VU_R_Y       8     // R电平条顶y(墨迹y8-12)
#define RP_CLIP_X       91    // 爆VU框x(电平条右端1px间隔)
#define RP_TIME_BASE    31    // 时间大字基线(helvB14墨迹y17-30)
#define RP_INFO_BASE    42    // 编码/大小行基线(4x6墨迹y37-41)
#define RP_SEP_Y        48    // 分割线y
#define RP_LABEL_VIEW_W 106   // 底部文件名可视宽度(右侧让电池图标)

// SONY D100风格电平行: 电平=1px竖线阵列(间隔1px), 峰值保持=2px宽竖线,
// 右端3px爆VU框(空心; 削波顶满时变实心, 提示爆音风险)
static void drawVuRowRec(U8G2* u8g2, int y, int level, int peak, bool clip) {
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    if (peak < 0) peak = 0;
    if (peak > 100) peak = 100;

    const int segments = RP_BAR_W / 2;              // 可显示的最大竖线根数
    int lit = (segments * level) / 100;
    for (int i = 0; i < lit; i++) {
        u8g2->drawVLine(RP_BAR_X + i * 2, y, RP_BAR_H);   // 1px宽
    }

    // 峰值保持: 2px宽竖线(与1px电平线区分)
    if (peak > 0) {
        int pkX = RP_BAR_X + (segments * peak) / 100 * 2;
        if (pkX > RP_BAR_X + RP_BAR_W - 2) pkX = RP_BAR_X + RP_BAR_W - 2;
        u8g2->drawBox(pkX, y, 2, RP_BAR_H);
    }

    // 爆VU框: 空心=正常, 实心=削波(录音电平顶满, 有爆音风险)
    if (clip) {
        u8g2->drawBox(RP_CLIP_X, y, 3, RP_BAR_H);
    } else {
        u8g2->drawFrame(RP_CLIP_X, y, 3, RP_BAR_H);
    }
}

RecordPage::RecordPage(int source) : UIPage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT) {
    // 输入源(RecordSourcePage选定, 本页固定): 记录显示名并写入音频引擎
    // (路径未开时CMD_REC_SRC被忽略, 后续openRecordPath按recSourceV配置, 时序安全)
    if (source < REC_SRC_BUILTIN || source > REC_SRC_BOTH) source = REC_SRC_DEFAULT;
    audioPlayer.setRecSource(source);

    // 硬件就绪检查(TF卡+WM8960); 菜单进入时音频必空闲, 此处扫描TF卡安全
    AudioInitState st = audioPlayer.getInitState();
    if (st == AUDIO_INIT_NONE) {
        st = audioPlayer.init();   // 防御: 正常开机流程应在setup()已初始化
    }
    if (st == AUDIO_INIT_NO_CARD) {
        pageState = RP_NO_CARD;
        return;
    }
    if (st == AUDIO_INIT_NO_CODEC) {
        pageState = RP_NO_CODEC;
        return;
    }
    scanNextFile();
}

RecordPage::~RecordPage() {
    // 兜底停止(正常流程BACK已发停止命令); stopRecording是异步命令, 不阻塞析构
    audioPlayer.stopRecording();
}

// 确定/Record/FOLDER01下的下一个录音文件名(REC001~REC999自动递增)
void RecordPage::scanNextFile() {
    fs::FS& fs = audioPlayer.getFS();

    // 确保目录链存在
    if (!fs.exists("/Record")) fs.mkdir("/Record");
    if (!fs.exists("/Record/FOLDER01")) fs.mkdir("/Record/FOLDER01");

    File dir = fs.open("/Record/FOLDER01");
    if (!dir) {
        LOG_E("RP[录音] 无法打开录音目录");
        pageState = RP_DIR_ERROR;
        return;
    }

    // 扫描已有RECxxx.WAV, 取最大编号(大小写不敏感, 防手动放入的小写文件撞号)
    int maxNum = 0;
    File f = dir.openNextFile();
    while (f) {
        String name = f.name();          // core 3.x: 只返回文件名
        bool isDir = f.isDirectory();
        f.close();
        name.toUpperCase();
        // 形如"REC042.WAV"(长度10=REC+3位数字+.WAV)才参与编号解析
        if (!isDir && name.length() == 10 && name.startsWith("REC") && name.endsWith(".WAV")) {
            int n = name.substring(3, 6).toInt();
            if (n > maxNum) maxNum = n;
        }
        f = dir.openNextFile();
    }
    dir.close();

    if (maxNum >= 999) {
        LOG_E("RP[录音] FOLDER01已满(REC999)");
        pageState = RP_DIR_ERROR;
        return;
    }
    char num[8];
    snprintf(num, sizeof(num), "REC%03d", maxNum + 1);
    fileLabel = String("FOLDER01/") + num + ".WAV";
    fullPath = String("/Record/FOLDER01/") + num + ".WAV";
    fileReady = true;
    // 滚动前停留1秒(带符号比较: idleUntil在未来时差值为负不推进)
    scrollIdleUntil = millis() + 1000;
    LOG_I("RP[录音] 下一文件: %s", fileLabel.c_str());
}

// ================= 按键(按键任务执行; audioPlayer接口全部非阻塞入队) =================
void RecordPage::toggleRecord() {
    RecState rs = audioPlayer.getRecState();
    if (rs == REC_STANDBY) {
        if (fileReady) {
            triedStart = true;   // 标记本会话已尝试启动(用于失败检测, 见update)
            audioPlayer.startRecording(fullPath);
            LOG_I("RP[键] 开始录音 %s", fullPath.c_str());
        }
    } else if (rs == REC_RECORDING) {
        audioPlayer.pauseRecording();
        LOG_I("RP[键] 暂停");
    } else if (rs == REC_PAUSED) {
        audioPlayer.resumeRecording();
        LOG_I("RP[键] 继续");
    }
    // REC_IDLE(监听命令处理中/错误态): 忽略按键, 等路径就绪
}

void RecordPage::onButton5(void* context) {
    if (returning) return;
    toggleRecord();
}

void RecordPage::onButtonEnter(void* context) {
    if (returning) return;
    toggleRecord();
}

void RecordPage::onButton2(void* context) {
    if (returning) return;
    audioPlayer.setRecGain(audioPlayer.getRecGainDb() + REC_GAIN_STEP);   // 2: 增益+
    LOG_I("RP[键] 增益+ -> %ddB", audioPlayer.getRecGainDb());
}

void RecordPage::onButton8(void* context) {
    if (returning) return;
    audioPlayer.setRecGain(audioPlayer.getRecGainDb() - REC_GAIN_STEP);   // 8: 增益-
    LOG_I("RP[键] 增益- -> %ddB", audioPlayer.getRecGainDb());
}

void RecordPage::requestReturn() {
    if (returning) return;
    returning = true;
    returnAt = millis();
    audioPlayer.stopRecording();   // 异步命令(引擎收尾: 剩余落盘+WAV头回写+关路径)
}

void RecordPage::onButtonBack(void* context) {
    if (returning) return;
    LOG_I("RP[键] BACK(停止录音并返回)");
    requestReturn();
}

// ================= 帧更新(渲染任务) =================
void RecordPage::update() {
    // 错误态: 提示1.5秒后自动返回
    if (pageState != RP_OK) {
        if (errorAt == 0) errorAt = millis();
        if (millis() - errorAt > 1500) {
            uiEngine.navigateBack();
        }
        return;
    }

    // 返回流程: 等录音引擎收尾完成(状态回IDLE=文件已落盘路径已断电)再导航;
    // 写卡错误时多停留1.5秒让用户看到提示; 4秒超时兜底
    if (returning) {
        bool err = audioPlayer.getRecError();
        bool idle = (audioPlayer.getRecState() == REC_IDLE);
        if ((idle && millis() - returnAt > (err ? 1500 : 0)) ||
            millis() - returnAt > 4000) {
            uiEngine.navigateBack();
        }
        return;
    }

    // 进入页面一次性发监听命令(开录音路径: 电平表活动可预调增益, 不写文件)
    if (!armed) {
        armed = true;
        audioPlayer.armRecording();
    }

    // 录音中途失败(建文件/写卡错误): 自动进入返回流程展示错误提示
    // (triedStart限定本会话, 不受上次录音会话残留的recError影响)
    if (triedStart && audioPlayer.getRecError() && !returning) {
        LOG_E("RP[录音] 检测到录音失败, 自动返回");
        requestReturn();
    }

    // 暂停时时间闪烁(500ms周期)
    if (audioPlayer.getRecState() == REC_PAUSED) {
        if (millis() - lastBlinkAt > 500) {
            lastBlinkAt = millis();
            blinkOn = !blinkOn;
        }
    } else {
        blinkOn = true;
    }

    // 文件名滚动
    updateScroll();

    // 电池电量(周期刷新缓存, 不触发ADC)
    if (millis() - lastBatteryCheck > 2000) {
        lastBatteryCheck = millis();
        batteryLevel = voltageToLevel(batteryManager.getLastVoltage());
    }

    // SD剩余空间(待机时界面显示用, 10秒刷新; 录音中音频任务在写卡, 此处不查)
    RecState st = audioPlayer.getRecState();
    if (st == REC_STANDBY && (lastSdFreeAt == 0 || millis() - lastSdFreeAt > 10000)) {
        lastSdFreeAt = millis();
        uint64_t freeB = SD_MMC.totalBytes() - SD_MMC.usedBytes();
        char b[10];
        snprintf(b, sizeof(b), "%.1fG", freeB / 1073741824.0);
        sdFreeStr = b;
    }
}

// 文件名超长无缝滚动: 进入页面停留1秒后开始(双份文本绘制, 视觉无缝衔接)
void RecordPage::updateScroll() {
    if (labelWidth <= RP_LABEL_VIEW_W) return;   // 不超长不滚
    if ((long)(millis() - scrollIdleUntil) < 0) return;   // 停留期内不滚
    if (millis() - scrollTimer <= 35) return;
    scrollTimer = millis();
    scrollOffset += 2;
    const int gap = 16;
    const int cycle = labelWidth + gap;
    if (scrollOffset >= cycle) {
        scrollOffset -= cycle;
    }
}

// ================= 绘制(渲染任务调用) =================
void RecordPage::render(U8G2* u8g2) {
    // 错误态: 居中提示
    if (pageState != RP_OK) {
        u8g2->setFont(FONT_CN);
        const char* msg = (pageState == RP_NO_CARD) ? "未检测到TF卡" :
                          (pageState == RP_NO_CODEC) ? "音频芯片无响应" : "录音目录异常或已满";
        uint16_t w = u8g2->getUTF8Width(msg);
        u8g2->drawUTF8((SCREEN_WIDTH - w) / 2, 34, msg);
        return;
    }

    // 返回流程: 收尾提示(写卡错误时提示错误)
    if (returning) {
        u8g2->setFont(FONT_CN);
        const char* msg = audioPlayer.getRecError() ? "TF卡写入错误" : "停止中...";
        uint16_t w = u8g2->getUTF8Width(msg);
        u8g2->drawUTF8((SCREEN_WIDTH - w) / 2, 34, msg);
        return;
    }

    RecState rs = audioPlayer.getRecState();

    drawLevelMeter(u8g2);
    drawRecRow(u8g2);
    drawInfoRow(u8g2);
    u8g2->drawHLine(0, RP_SEP_Y, SCREEN_WIDTH);

    drawBottomRow(u8g2);
}

// 电平区(两行): L/R标签 + 电平条 + 爆VU框 | 右侧录音增益(与L/R两行居中对齐)
void RecordPage::drawLevelMeter(U8G2* u8g2) {
    u8g2->setFont(FONT_TINY);
    u8g2->drawStr(0, RP_VU_L_Y + 5, "L");     // 基线对齐L条墨迹底
    u8g2->drawStr(0, RP_VU_R_Y + 5, "R");     // 基线对齐R条墨迹底
    drawVuRowRec(u8g2, RP_VU_L_Y, audioPlayer.getVuL(), audioPlayer.getPeakL(), audioPlayer.getRecClipL());
    drawVuRowRec(u8g2, RP_VU_R_Y, audioPlayer.getVuR(), audioPlayer.getPeakR(), audioPlayer.getRecClipR());

    // 录音增益(如"-12dB"): 右对齐, 与L/R两行垂直居中
    u8g2->setFont(FONT_SMALL);
    char gbuf[8];
    int g = audioPlayer.getRecGainDb();
    if (g > 0)      snprintf(gbuf, sizeof(gbuf), "+%ddB", g);
    else if (g < 0) snprintf(gbuf, sizeof(gbuf), "%ddB", g);   // 自带负号
    else            snprintf(gbuf, sizeof(gbuf), "0dB");
    int gw = u8g2->getStrWidth(gbuf);
    u8g2->drawStr(SCREEN_WIDTH - 1 - gw, 11, gbuf);
}

// REC/PAUSE反色块(左, 仅录音/暂停) + 录音时间(居右大字, 暂停时闪烁)
void RecordPage::drawRecRow(U8G2* u8g2) {
    RecState rs = audioPlayer.getRecState();
    u8g2->setFont(FONT_BIG);

    // REC/PAUSE标签: 反色块(黑底白字); 录音中常显REC, 暂停时PAUSE随时间闪烁
    if (rs == REC_RECORDING || (rs == REC_PAUSED && blinkOn)) {
        const char* tag = (rs == REC_RECORDING) ? "REC" : "PAUSE";
        u8g2->setFont(FONT_SMALL);
        uint16_t tw = u8g2->getStrWidth(tag);
        // 块高11: profont11墨迹(b-7..b-1)上下各留2px, 与时间行垂直居中
        u8g2->drawBox(0, RP_TIME_BASE - 9, tw + 6, 11);
        u8g2->setFontMode(1);
        u8g2->setDrawColor(0);
        u8g2->drawStr(3, RP_TIME_BASE - 1, tag);
        u8g2->setDrawColor(1);
        u8g2->setFontMode(0);
    }

    // 时间"1h23m45s": 居右常显; 暂停时500ms闪烁(blinkOn为false的那半周期不画)
    if (!(rs == REC_PAUSED && !blinkOn)) {
        u8g2->setFont(FONT_BIG);
        uint32_t s = audioPlayer.getRecElapsedMs() / 1000;
        char tbuf[12];
        snprintf(tbuf, sizeof(tbuf), "%luh%02lum%02lus",
                 (unsigned long)(s / 3600),
                 (unsigned long)((s / 60) % 60),
                 (unsigned long)(s % 60));
        int tw = u8g2->getStrWidth(tbuf);
        u8g2->drawStr(SCREEN_WIDTH - 1 - tw, RP_TIME_BASE, tbuf);
    }
}

// 编码格式(左) + 文件大小(录音/暂停)或SD剩余空间(待机)(右)
void RecordPage::drawInfoRow(U8G2* u8g2) {
    u8g2->setFont(FONT_TINY);
    u8g2->drawStr(0, RP_INFO_BASE, "LPCM 44.1k/16bit");

    char rbuf[12];
    rbuf[0] = '\0';
    RecState rs = audioPlayer.getRecState();
    if (rs == REC_RECORDING || rs == REC_PAUSED) {
        const float sizeMB = audioPlayer.getRecFileSize() / 1048576.0f;
        if (sizeMB >= 1024.0f) {
            snprintf(rbuf, sizeof(rbuf), "%.1fG", sizeMB / 1024.0f);
        } else if (sizeMB >= 100.0f) {
            snprintf(rbuf, sizeof(rbuf), "%.0fMB", sizeMB);
        } else {
            snprintf(rbuf, sizeof(rbuf), "%.1fMB", sizeMB);
        }
    } else if (sdFreeStr.length() > 0) {
        snprintf(rbuf, sizeof(rbuf), "%s", sdFreeStr.c_str());
    }
    if (rbuf[0]) {
        int rw = u8g2->getStrWidth(rbuf);
        u8g2->drawStr(SCREEN_WIDTH - 1 - rw, RP_INFO_BASE, rbuf);
    }
}

// 底部: 当前文件名(左, 超长滚动) + 电池图标(右)
void RecordPage::drawBottomRow(U8G2* u8g2) {
    if (fileLabel.length() > 0) {
        u8g2->setFont(FONT_LABEL);
        labelWidth = u8g2->getStrWidth(fileLabel.c_str());
        if (labelWidth <= RP_LABEL_VIEW_W) {
            u8g2->drawStr(0, 63, fileLabel.c_str());
        } else {
            // 无缝连续滚动: 视图里始终放两份文本, 第一份完全移出后偏移回退一个周期
            const int gap = 16;
            const int cycle = labelWidth + gap;
            int x = -scrollOffset;
            u8g2->setClipWindow(0, 56, RP_LABEL_VIEW_W + 1, 64);
            u8g2->drawStr(x, 63, fileLabel.c_str());
            u8g2->drawStr(x + cycle, 63, fileLabel.c_str());
            u8g2->setMaxClipWindow();
        }
    }

    // 电池(与播放页同款: 旋转绘制的专用电池字体, glyph 48..53 = 0..5格)
    u8g2->setFont(u8g2_font_battery19_tn);
    u8g2->setFontDirection(3);
    u8g2->drawGlyph(SCREEN_WIDTH, 62, 48 + batteryLevel);
    u8g2->setFontDirection(0);
}

// ================= 工具 =================
int RecordPage::voltageToLevel(float voltage) {
    if (voltage >= 4.0) return 5;
    if (voltage >= 3.8) return 4;
    if (voltage >= 3.6) return 3;
    if (voltage >= 3.4) return 2;
    if (voltage >= 3.2) return 1;
    return 0;
}
