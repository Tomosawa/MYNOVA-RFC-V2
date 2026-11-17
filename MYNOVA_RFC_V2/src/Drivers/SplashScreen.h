/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

// SplashScreen.h
#ifndef SplashScreen_h
#define SplashScreen_h

#include <Arduino.h>
#include <U8g2lib.h>

// 开机动画样式（与 SystemConfig.bootAnim 对应）
#define BOOT_ANIM_OFF     0  // 关闭（极速开机）
#define BOOT_ANIM_WAVE    1  // 波浪 LOGO（默认）
#define BOOT_ANIM_SCAN    2  // 雷达扫描
#define BOOT_ANIM_STAR    3  // 星空粒子
#define BOOT_ANIM_TYPE    4  // 打字机
#define BOOT_ANIM_MATRIX  5  // 矩阵雨
#define BOOT_ANIM_COUNT   6

class SplashScreen {
public:
    SplashScreen();
    
    void init(U8G2* u8g2);
    void setStyle(int style);
    int getStyle() { return animStyle; }
    void setProgress(int progress, const char* statusText = nullptr);
    void render();
    void tick(unsigned long ms);  // 等待期间持续刷新动画帧(替代裸delay, 保证动画流畅)
    void finish();
    
private:
    U8G2* pU8g2;
    int currentProgress;
    const char* currentStatus;
    unsigned long animStartTime;
    int animFrame;
    int animStyle;

    void drawLogo(int centerY);
    void drawLogoStatic(int centerY);
    void drawProgressBar(int y, int progress);
    void drawStatus(int y);

    // 各样式渲染(独立设计, 不共享LOGO/进度条模板)
    void renderWave();
    void renderScan();
    void renderStar();
    void renderType();
    void renderMatrix();
    void pushTypeLine(const char* status);  // 打字机: 追加一行终端日志
    
    // 打字机终端日志缓冲(最多7行, 屏显6行滚动)
    char typeLines[7][24];
    int typeLineCount;
    unsigned long typeLineTime;
    
    int getWaveOffset(int charIndex, int frame);
};

#endif
