/*
  RFSubGhzPresets.h — SubGHz 调制预设（移植自 Flipper cc1101_configs.c）

  参考：flipperzero-firmware-dev/lib/subghz/devices/cc1101_configs.c
       flipperzero-firmware-dev/lib/subghz/devices/preset.h

  每个预设为 Flipper 原版 (寄存器地址, 值) 对序列，以 {0,0} 结束，
  其后 8 字节为 PATABLE（发射功率表）。接收应用时只取 {0,0} 之前的
  寄存器对，IOCFG2 由上层强制为 0x0D（本硬件异步数据从 GDO2 输出）。
*/

#ifndef RF_SUBGHZ_PRESETS_H
#define RF_SUBGHZ_PRESETS_H

#include <Arduino.h>
#include <stdint.h>

// 预设 ID（顺序即配置页中的循环顺序，默认 AM270=0）
enum RFSubGhzPresetId {
    RF_PRESET_AM270 = 0,   // OOK 270kHz 异步（常规遥控，默认）
    RF_PRESET_AM650,       // OOK 650kHz 异步（弱信号/宽带宽）
    RF_PRESET_FM238,       // 2FSK 频偏 2.38kHz（FM 类遥控/车库门）
    RF_PRESET_FM476,       // 2FSK 频偏 47.6kHz（FM 类遥控）
    RF_PRESET_COUNT
};

struct RFSubGhzPreset {
    const char* displayName;    // 配置页短名
    const char* flipperName;    // .sub 文件 Preset 字段原值
    bool        isFM;           // true=2FSK（发射走 FM 路径），false=OOK
    const uint8_t* regPairs;    // (reg,val) 对序列，{0,0} 结束 + 8 字节 PATABLE
};

// ── OOK 270kHz Async（MDMCFG4=0x67, AGC 03/00/40，与现网接收基线一致）──
static const uint8_t RF_PRESET_REGS_AM270[] = {
    0x02, 0x0D,                 // IOCFG0  异步串行数据
    0x03, 0x47,                 // FIFOTHR
    0x08, 0x32,                 // PKTCTRL0 异步连续、无白化
    0x0B, 0x06,                 // FSCTRL1
    0x14, 0x00,                 // MDMCFG0（0x0E 是 FREQ1）
    0x13, 0x00,                 // MDMCFG1
    0x12, 0x30,                 // MDMCFG2 ASK/OOK，无同步
    0x11, 0x32,                 // MDMCFG3 数据率 ~3.79kBaud
    0x10, 0x67,                 // MDMCFG4 RX BW 270kHz
    0x18, 0x18,                 // MCSM0   IDLE→RX/TX 自动校准
    0x19, 0x18,                 // FOCCFG
    0x1D, 0x40,                 // AGCCTRL0
    0x1C, 0x00,                 // AGCCTRL1
    0x1B, 0x03,                 // AGCCTRL2 MAGN_TARGET 24dB
    0x20, 0xFB,                 // WORCTRL
    0x22, 0x11,                 // FREND0  OOK 用 PATABLE[1]
    0x21, 0xB6,                 // FREND1
    0x00, 0x00,                 // 结束标记
    // OOK PATABLE: index0=载波关，index1=功率（运行时按功率档重写）
    0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ── OOK 650kHz Async（MDMCFG4=0x17，AGC 91/00/07）──
static const uint8_t RF_PRESET_REGS_AM650[] = {
    0x02, 0x0D,
    0x03, 0x07,                 // FIFOTHR
    0x08, 0x32,
    0x0B, 0x06,
    0x14, 0x00,
    0x13, 0x00,
    0x12, 0x30,
    0x11, 0x32,
    0x10, 0x17,                 // MDMCFG4 RX BW 650kHz
    0x18, 0x18,
    0x19, 0x18,
    0x1D, 0x91,                 // AGCCTRL0
    0x1C, 0x00,
    0x1B, 0x07,                 // AGCCTRL2 MAGN_TARGET 42dB
    0x20, 0xFB,
    0x22, 0x11,
    0x21, 0xB6,
    0x00, 0x00,
    0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ── 2FSK Dev 2.38kHz Async ──
static const uint8_t RF_PRESET_REGS_FM238[] = {
    0x02, 0x0D,
    0x03, 0x47,
    0x08, 0x32,
    0x07, 0x04,                 // PKTCTRL1
    0x0B, 0x06,
    0x14, 0x00,
    0x13, 0x02,                 // MDMCFG1
    0x12, 0x04,                 // MDMCFG2 2-FSK，无同步
    0x11, 0x83,                 // MDMCFG3 数据率 ~4.8kBaud
    0x10, 0x67,                 // MDMCFG4 RX BW 270kHz
    0x15, 0x04,                 // DEVIATN 频偏 2.380kHz
    0x18, 0x18,
    0x19, 0x16,                 // FOCCFG
    0x1D, 0x91,                 // AGCCTRL0
    0x1C, 0x00,
    0x1B, 0x07,
    0x20, 0xFB,
    0x22, 0x10,                 // FREND0 固定 PA（PATABLE[0]）
    0x21, 0x56,                 // FREND1
    0x00, 0x00,
    // 2FSK PATABLE: 单功率项（运行时按功率档重写 index0）
    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ── 2FSK Dev 47.6kHz Async ──
static const uint8_t RF_PRESET_REGS_FM476[] = {
    0x02, 0x0D,
    0x03, 0x47,
    0x08, 0x32,
    0x07, 0x04,
    0x0B, 0x06,
    0x14, 0x00,
    0x13, 0x02,
    0x12, 0x04,                 // MDMCFG2 2-FSK
    0x11, 0x83,
    0x10, 0x67,
    0x15, 0x47,                 // DEVIATN 频偏 47.607kHz
    0x18, 0x18,
    0x19, 0x16,
    0x1D, 0x91,
    0x1C, 0x00,
    0x1B, 0x07,
    0x20, 0xFB,
    0x22, 0x10,
    0x21, 0x56,
    0x00, 0x00,
    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// 预设显示名说明（紧凑无空格：录制页顶行与频率/样本数同行显示，128px 放不下长名；
// flipperName 保持 Flipper 原名以兼容 .sub 生态）：
//   OOK270k / OOK650k —— 都是 OOK(ASK) 调制，区别仅是接收中频带宽：
//       270kHz 窄带抗噪（默认，普通遥控器），650kHz 宽带边沿响应快（快速/弱信号）
//   FM2.4k / FM47.6k  —— 2FSK 调制，数字为频偏（deviation 2.38k/47.6k Hz）
static const RFSubGhzPreset RF_SUBGHZ_PRESETS[RF_PRESET_COUNT] = {
    {"OOK270k", "FuriHalSubGhzPresetOok270Async",      false, RF_PRESET_REGS_AM270},
    {"OOK650k", "FuriHalSubGhzPresetOok650Async",      false, RF_PRESET_REGS_AM650},
    {"FM2.4k",  "FuriHalSubGhzPreset2FSKDev238Async",  true,  RF_PRESET_REGS_FM238},
    {"FM47.6k", "FuriHalSubGhzPreset2FSKDev476Async",  true,  RF_PRESET_REGS_FM476},
};

inline const RFSubGhzPreset& RfPreset(uint8_t id) {
    return RF_SUBGHZ_PRESETS[(id < RF_PRESET_COUNT) ? id : 0];
}

// 按 Flipper Preset 名反查 ID（未知名/空名回退 AM270）
inline uint8_t RfPresetIdFromName(const char* flipperName) {
    if (flipperName != nullptr) {
        for (uint8_t i = 0; i < RF_PRESET_COUNT; i++) {
            if (strcmp(flipperName, RF_SUBGHZ_PRESETS[i].flipperName) == 0) return i;
        }
    }
    return RF_PRESET_AM270;
}

#endif // RF_SUBGHZ_PRESETS_H
