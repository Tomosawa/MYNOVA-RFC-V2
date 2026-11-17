/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

// SplashScreen.cpp
#include "SplashScreen.h"

static const char* LOGO_TEXT = "MYNOVA";
static const int LOGO_CHAR_COUNT = 6;

static const int SCREEN_W = 128;
static const int SCREEN_H = 64;

SplashScreen::SplashScreen() {
    pU8g2 = nullptr;
    currentProgress = 0;
    currentStatus = "Starting...";
    animStartTime = 0;
    animFrame = 0;
    animStyle = BOOT_ANIM_WAVE;
    typeLineCount = 0;
    typeLineTime = 0;
}

void SplashScreen::init(U8G2* u8g2) {
    pU8g2 = u8g2;
    animStartTime = millis();
    animFrame = 0;
    typeLineCount = 0;
    typeLineTime = 0;
}

void SplashScreen::setStyle(int style) {
    if (style < 0) style = 0;
    if (style >= BOOT_ANIM_COUNT) style = BOOT_ANIM_WAVE;
    animStyle = style;
}

void SplashScreen::setProgress(int progress, const char* statusText) {
    currentProgress = progress;
    if (statusText) {
        currentStatus = statusText;
    }
    if (animStyle == BOOT_ANIM_OFF) return;  // 关闭动画: 完全不渲染不等待, 直接进首页
    if (animStyle == BOOT_ANIM_TYPE) {
        pushTypeLine(currentStatus);  // 终端模式: 每个初始化阶段追加一行日志
    }
    render();
}

// 等待期间持续刷新动画帧(替代裸delay): 原来动画只在setProgress时渲染一帧,
// 各模块init之间的delay(50)全是黑屏间隙, 动画呈幻灯片跳变; tick让等待期动起来
void SplashScreen::tick(unsigned long ms) {
    if (!pU8g2 || animStyle == BOOT_ANIM_OFF) {
        delay(ms);
        return;
    }
    unsigned long endT = millis() + ms;
    while ((long)(millis() - endT) < 0) {
        render();
        delay(25);
    }
}

int SplashScreen::getWaveOffset(int charIndex, int frame) {
    float phase = (float)charIndex * 0.8f + (float)frame * 0.15f;
    return (int)(sin(phase) * 4.0f);
}

void SplashScreen::drawLogoStatic(int centerY) {
    if (!pU8g2) return;
    pU8g2->setFont(u8g2_font_helvB14_tr);
    int logoWidth = pU8g2->getStrWidth(LOGO_TEXT);
    pU8g2->drawStr((SCREEN_W - logoWidth) / 2, centerY, LOGO_TEXT);
    pU8g2->setFont(u8g2_font_5x7_tr);
    const char* subtitle = "RF Controller";
    int subWidth = pU8g2->getStrWidth(subtitle);
    pU8g2->drawStr((SCREEN_W - subWidth) / 2, centerY + 12, subtitle);
}

void SplashScreen::drawLogo(int centerY) {
    if (!pU8g2) return;
    pU8g2->setFont(u8g2_font_helvB14_tr);
    int logoWidth = pU8g2->getStrWidth(LOGO_TEXT);
    int startX = (SCREEN_W - logoWidth) / 2;
    unsigned long elapsed = millis() - animStartTime;
    animFrame = elapsed / 50;
    pU8g2->setDrawColor(1);
    int charX = startX;
    for (int i = 0; i < LOGO_CHAR_COUNT; i++) {
        char c[2] = {LOGO_TEXT[i], '\0'};
        int waveY = getWaveOffset(i, animFrame);
        pU8g2->drawStr(charX + 2, centerY + waveY + 2, c);
        charX += pU8g2->getStrWidth(c);
    }
    pU8g2->setDrawColor(0);
    charX = startX;
    for (int i = 0; i < LOGO_CHAR_COUNT; i++) {
        char c[2] = {LOGO_TEXT[i], '\0'};
        int waveY = getWaveOffset(i, animFrame);
        pU8g2->drawStr(charX, centerY + waveY, c);
        charX += pU8g2->getStrWidth(c);
    }
    pU8g2->setDrawColor(1);
    charX = startX;
    for (int i = 0; i < LOGO_CHAR_COUNT; i++) {
        char c[2] = {LOGO_TEXT[i], '\0'};
        int waveY = getWaveOffset(i, animFrame);
        pU8g2->drawStr(charX, centerY + waveY, c);
        charX += pU8g2->getStrWidth(c);
    }
    pU8g2->setFont(u8g2_font_5x7_tr);
    const char* subtitle = "RF Controller";
    int subWidth = pU8g2->getStrWidth(subtitle);
    pU8g2->drawStr((SCREEN_W - subWidth) / 2, centerY + 12, subtitle);
}

void SplashScreen::drawProgressBar(int y, int progress) {
    if (!pU8g2) return;
    const int barX = 14;
    const int barWidth = 100;
    const int barHeight = 8;
    pU8g2->setDrawColor(1);
    pU8g2->drawFrame(barX, y, barWidth, barHeight);
    int fillWidth = (barWidth - 4) * progress / 100;
    if (fillWidth > 0) {
        pU8g2->drawBox(barX + 2, y + 2, fillWidth, barHeight - 4);
        if (progress < 100 && (animFrame % 4) < 2) {
            int headX = barX + 2 + fillWidth - 2;
            if (headX > barX + 2) {
                pU8g2->setDrawColor(0);
                pU8g2->drawBox(headX, y + 2, 2, barHeight - 4);
                pU8g2->setDrawColor(1);
            }
        }
    }
    char percentText[8];
    sprintf(percentText, "%d%%", progress);
    pU8g2->setFont(u8g2_font_5x7_tr);
    pU8g2->drawStr(barX + barWidth + 4, y + 7, percentText);
}

void SplashScreen::drawStatus(int y) {
    if (!pU8g2 || !currentStatus) return;
    pU8g2->setFont(u8g2_font_5x7_tr);
    int textWidth = pU8g2->getStrWidth(currentStatus);
    int x = (SCREEN_W - textWidth) / 2;
    char dots[4] = {'.', '.', '.', '\0'};
    int dotCount = (animFrame / 4) % 4;
    dots[dotCount] = '\0';
    pU8g2->drawStr(x, y, currentStatus);
    if (currentProgress < 100) {
        pU8g2->drawStr(x + textWidth, y, dots);
    }
}

void SplashScreen::renderWave() {
    pU8g2->clearBuffer();
    pU8g2->setDrawColor(1);
    pU8g2->drawHLine(0, 0, SCREEN_W);
    pU8g2->drawHLine(0, 1, SCREEN_W);
    drawLogo(28);
    drawProgressBar(44, currentProgress);
    drawStatus(62);
    pU8g2->drawHLine(0, 63, SCREEN_W);
    pU8g2->sendBuffer();
}

void SplashScreen::renderScan() {
    // 雷达扫描·独立设计: 圆形雷达(外圆/内环/十字准线) + 旋转扫描线带余辉尾迹
    // + 扫描线扫过的目标点亮起; 无LOGO; 底部进度线+状态
    unsigned long elapsed = millis() - animStartTime;
    pU8g2->clearBuffer();
    pU8g2->setDrawColor(1);
    const int cx = 64, cy = 25, r = 22;
    // 雷达底盘: 外圆/两内环/十字准线(出头3px)
    pU8g2->drawCircle(cx, cy, r);
    pU8g2->drawCircle(cx, cy, r * 2 / 3);
    pU8g2->drawCircle(cx, cy, r / 3);
    pU8g2->drawLine(cx - r - 3, cy, cx + r + 3, cy);
    pU8g2->drawLine(cx, cy - r - 3, cx, cy + r + 3);
    // 旋转扫描线(~2.4s/圈, 0=正上, 顺时针) + 余辉(尾迹线角度递减/长度递减)
    float theta = (elapsed % 2400) * 2.0f * PI / 2400.0f;
    pU8g2->drawLine(cx, cy, cx + (int)(r * sinf(theta)), cy - (int)(r * cosf(theta)));
    const float trail[3][2] = {{0.16f, 0.85f}, {0.36f, 0.65f}, {0.62f, 0.42f}};
    for (int t = 0; t < 3; t++) {
        float a = theta - trail[t][0];
        if (a < 0) a += 2.0f * PI;
        pU8g2->drawLine(cx, cy, cx + (int)(r * trail[t][1] * sinf(a)),
                        cy - (int)(r * trail[t][1] * cosf(a)));
    }
    // 目标点: 扫描线扫过后亮起, 下一圈临近时熄灭
    for (int i = 0; i < 4; i++) {
        float ta = i * 1.7f + 0.6f;                 // 目标角度(rad, 分散四象限)
        float tr = r * (0.45f + (i % 3) * 0.2f);    // 目标距离(圆内)
        float diff = theta - ta;
        while (diff < 0) diff += 2.0f * PI;
        while (diff >= 2.0f * PI) diff -= 2.0f * PI;
        if (diff < 2.0f * PI - 0.35f) {             // 已扫过且未到下一圈
            int px = cx + (int)(tr * sinf(ta));
            int py = cy - (int)(tr * cosf(ta));
            pU8g2->drawBox(px - 1, py - 1, 3, 3);
        }
    }
    // 底部: 分隔线+进度线+状态
    pU8g2->drawHLine(0, 52, SCREEN_W);
    int fw = (SCREEN_W - 8) * currentProgress / 100;
    if (fw > 0) pU8g2->drawBox(4, 55, fw, 2);
    pU8g2->setFont(u8g2_font_5x7_tr);
    int tw = pU8g2->getStrWidth(currentStatus);
    pU8g2->drawStr((SCREEN_W - tw) / 2, 63, currentStatus);
    pU8g2->sendBuffer();
}

void SplashScreen::renderStar() {
    // 星空流星·独立设计: 三层视差星野下落 + 周期流星, 无LOGO
    // 远景慢闪/中景十字亮星/近景2x2快落; 进度=底部点阵表
    unsigned long elapsed = millis() - animStartTime;
    int frame = elapsed / 60;
    pU8g2->clearBuffer();
    pU8g2->setDrawColor(1);
    // 远景星层: 慢速下落+闪烁
    for (int i = 0; i < 14; i++) {
        int sx = (i * 89 + 7) % SCREEN_W;
        int sy = (i * 31 + frame) % 50;
        if (((i + frame / 6) % 4) != 0)
            pU8g2->drawPixel(sx, sy);
    }
    // 中景星层: 稍快, 部分亮星(十字)
    for (int i = 0; i < 7; i++) {
        int sx = (i * 151 + 43) % SCREEN_W;
        int sy = (i * 47 + frame * 2) % 50;
        pU8g2->drawPixel(sx, sy);
        if (((i + frame / 4) % 3) == 0) {
            pU8g2->drawPixel(sx - 1, sy);
            pU8g2->drawPixel(sx + 1, sy);
            pU8g2->drawPixel(sx, sy - 1);
            pU8g2->drawPixel(sx, sy + 1);
        }
    }
    // 近景星层: 2x2 快速下落
    for (int i = 0; i < 4; i++) {
        int sx = (i * 211 + 13) % (SCREEN_W - 2);
        int sy = (i * 19 + frame * 3) % 48;
        pU8g2->drawBox(sx, sy, 2, 2);
    }
    // 流星: 每~2.2s一颗, 右上→左下, 头部亮点+渐稀尾迹
    int mPhase = frame % 36;
    if (mPhase < 11) {
        int mx = 122 - mPhase * 10;
        int my = 3 + mPhase * 4;
        pU8g2->drawBox(mx, my, 2, 2);
        for (int t = 1; t <= 4; t++) {
            int tx = mx + t * 5;
            int ty = my - t * 2;
            if (tx < SCREEN_W && ty >= 0)
                pU8g2->drawPixel(tx, ty);
            if (t <= 2 && tx + 1 < SCREEN_W && ty >= 1)
                pU8g2->drawPixel(tx + 1, ty - 1);
        }
    }
    // 进度: 底部点阵表(点亮段=3x3块, 空段=1px点)
    int segs = (currentProgress * 16 + 99) / 100;
    for (int s = 0; s < 16; s++) {
        int sx = 5 + s * 7;
        if (s < segs) pU8g2->drawBox(sx - 1, 52, 3, 3);
        else pU8g2->drawPixel(sx, 53);
    }
    // 状态文字
    pU8g2->setFont(u8g2_font_5x7_tr);
    int tw = pU8g2->getStrWidth(currentStatus);
    pU8g2->drawStr((SCREEN_W - tw) / 2, 63, currentStatus);
    pU8g2->sendBuffer();
}

void SplashScreen::pushTypeLine(const char* status) {
    if (typeLineCount >= 7) {  // 缓冲满则滚动
        memmove(typeLines[0], typeLines[1], 6 * 24);
        typeLineCount = 6;
    }
    snprintf(typeLines[typeLineCount], 24, "> %s [OK]", status);
    typeLineCount++;
    typeLineTime = millis();
}

void SplashScreen::renderType() {
    // 打字机·独立设计: 终端启动日志(类Linux boot console)
    // 每个初始化阶段一行日志, 最后一行逐字打出+光标闪烁; 顶栏显示百分比
    pU8g2->clearBuffer();
    pU8g2->setDrawColor(1);
    pU8g2->setFont(u8g2_font_5x7_tr);
    // 顶栏: 标题+百分比
    pU8g2->drawStr(1, 7, "MYNOVA BOOT");
    char pct[6];
    snprintf(pct, sizeof(pct), "%3d%%", currentProgress);
    pU8g2->drawStr(SCREEN_W - pU8g2->getStrWidth(pct) - 1, 7, pct);
    pU8g2->drawHLine(0, 9, SCREEN_W);
    // 日志区: 最多6行, 满后滚动
    int shown = typeLineCount > 6 ? 6 : typeLineCount;
    int startLine = typeLineCount - shown;
    for (int l = 0; l < shown; l++) {
        const char* line = typeLines[startLine + l];
        bool isLast = (startLine + l == typeLineCount - 1);
        int full = strlen(line);
        int chars = full;
        if (isLast) {  // 最后一行: 逐字打出
            unsigned long dt = millis() - typeLineTime;
            chars = dt / 35;
            if (chars > full) chars = full;
        }
        char buf[24];
        memcpy(buf, line, chars);
        buf[chars] = 0;
        int ly = 17 + l * 8;
        pU8g2->drawStr(2, ly, buf);
        if (isLast) {  // 光标块(打字中常亮, 打完闪烁)
            int cxx = 2 + chars * 6;
            if (chars < full || ((millis() / 300) % 2) == 0)
                pU8g2->drawBox(cxx, ly - 7, 5, 7);
        }
    }
    if (typeLineCount == 0)
        pU8g2->drawStr(2, 17, "> BOOTING...");
    pU8g2->sendBuffer();
}

void SplashScreen::renderMatrix() {
    // 矩阵雨·独立设计: 全屏16列字符雨, 每列独立速度/长度/相位
    // 头部实心亮块+尾迹随机字符缓变; 底部隔离带内一条粗进度线
    unsigned long elapsed = millis() - animStartTime;
    int frame = elapsed / 70;
    pU8g2->clearBuffer();
    pU8g2->setDrawColor(1);
    pU8g2->setFont(u8g2_font_5x7_tr);
    for (int col = 0; col < 16; col++) {
        int speed = 1 + (col * 7 + 3) % 3;                    // 每帧步进1..3字符行
        int len = 3 + (col * 13 + 5) % 5;                     // 雨长3..7字符
        int head = (frame * speed + col * 17) % (7 + len) - len;  // -len..6
        for (int s = 0; s < len; s++) {
            int cy = head - s;
            if (cy < 0 || cy > 6) continue;                   // 字符行0..6(y0..55)
            char c = 33 + (col * 31 + s * 7 + frame / 4) % 90;  // 随机字符缓变
            char buf[2] = {c, '\0'};
            pU8g2->drawStr(col * 8, cy * 8 + 7, buf);
        }
        if (head >= 0 && head <= 6)                           // 头部亮块
            pU8g2->drawBox(col * 8, head * 8, 5, 7);
    }
    // 底部隔离带: 黑底遮住雨滴, 进度=粗进度线
    pU8g2->setDrawColor(0);
    pU8g2->drawBox(0, 57, SCREEN_W, 7);
    pU8g2->setDrawColor(1);
    pU8g2->drawHLine(0, 57, SCREEN_W);
    int fw = (SCREEN_W - 4) * currentProgress / 100;
    if (fw > 0) pU8g2->drawBox(2, 59, fw, 4);
    pU8g2->sendBuffer();
}

void SplashScreen::render() {
    if (!pU8g2) return;
    if (animStyle == BOOT_ANIM_OFF) return;
    switch (animStyle) {
        case BOOT_ANIM_SCAN:   renderScan(); break;
        case BOOT_ANIM_STAR:   renderStar(); break;
        case BOOT_ANIM_TYPE:   renderType(); break;
        case BOOT_ANIM_MATRIX: renderMatrix(); break;
        case BOOT_ANIM_WAVE:
        default:               renderWave(); break;
    }
}

void SplashScreen::finish() {
    if (!pU8g2) return;
    if (animStyle == BOOT_ANIM_OFF) return;  // 关闭动画：直接进主页
    for (int i = 0; i < 5; i++) {
        pU8g2->clearBuffer();
        if (i % 2 == 0) {
            pU8g2->setFont(u8g2_font_helvB14_tr);
            int logoWidth = pU8g2->getStrWidth(LOGO_TEXT);
            pU8g2->drawStr((SCREEN_W - logoWidth) / 2, 32, LOGO_TEXT);
        }
        pU8g2->sendBuffer();
        delay(80);
    }
    for (int r = 0; r <= 80; r += 10) {
        pU8g2->clearBuffer();
        pU8g2->setFont(u8g2_font_helvB14_tr);
        int logoWidth = pU8g2->getStrWidth(LOGO_TEXT);
        pU8g2->drawStr((SCREEN_W - logoWidth) / 2, 32, LOGO_TEXT);
        pU8g2->setDrawColor(0);
        pU8g2->drawDisc(SCREEN_W / 2, SCREEN_H / 2, r);
        pU8g2->setDrawColor(1);
        pU8g2->sendBuffer();
        delay(30);
    }
}
