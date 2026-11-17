/* 
* Copyright (c) 2026 Tomosawa 
* https://github.com/Tomosawa/ 
* All rights reserved 
*/

#ifndef H_IOPIN_H
#define H_IOPIN_H

// 电池检测引脚
#define PIN_BATTERY     9

// CC1101 射频芯片引脚 (SPI接口)
#define PIN_CC1101_SCK  42  // SPI SCK
#define PIN_CC1101_MISO 41  // SPI MISO
#define PIN_CC1101_MOSI 2   // SPI MOSI
#define PIN_CC1101_CS   38  // SPI CS（CC1101片选）
#define PIN_CC1101_GDO0 39  // GDO0：异步数据，定频/RAW接收与发射复用
#define PIN_CC1101_GDO2 40  // GDO2：扫频和既有协议的异步数据中断

// 外部天线开关控制（低频段310~350MHz=LOW，高频段390~434MHz=HIGH；HIGH选择高频通路）
#define PIN_RF_SW_CTRL  47

// 红外收发引脚
#define PIN_IR_RECV     45  // 红外接收头（TSOP系列，输出低有效）
#define PIN_IR_SEND     46  // 红外发射LED（经三极管驱动）

// OLED显示屏引脚
#define PIN_OLED_SCL    0
#define PIN_OLED_SDA    1

// TF卡引脚 (SDMMC 1位模式)
#define PIN_SD_CMD      37
#define PIN_SD_CLK      36
#define PIN_SD_D0       35

// WM8960 音频编解码器引脚 (I2C与OLED共用总线: SCL=0 SDA=1)
#define PIN_I2S_MCLK    10  // WM8960 MCLK (256×fs)
#define PIN_I2S_BCLK    11  // WM8960 BCLK
#define PIN_I2S_WS      12  // WM8960 DACLRC (I2S控制器的WS)
#define PIN_I2S_ALRC    14  // WM8960 ADCLRC (GPIO矩阵复制WS信号)
#define PIN_I2S_DOUT    13  // WM8960 DACDAT (ESP32 -> WM8960 播放)
#define PIN_I2S_DIN     21  // WM8960 ADCDAT (WM8960 -> ESP32 录音)

// 按键引脚
#define PIN_KEY_ENTER   4
#define PIN_KEY_MENU    5
#define PIN_KEY_BACK    6
#define PIN_KEY_1       20
#define PIN_KEY_2       19
#define PIN_KEY_3       8
#define PIN_KEY_4       18
#define PIN_KEY_5       17
#define PIN_KEY_6       16
#define PIN_KEY_7       15
#define PIN_KEY_8       7
#define PIN_KEY_9       3


#endif
