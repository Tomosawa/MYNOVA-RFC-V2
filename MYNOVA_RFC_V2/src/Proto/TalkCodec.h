/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// TalkCodec.h - IMA-ADPCM 编解码(4bit, 单声道16位PCM互转)
//   仅头文件内联实现, TalkManager 与 AudioPlayer 共用, 无堆分配/无浮点。
//   16kHz下 384样本→192字节(24ms/帧, 约64kbps); 8kHz下384样本→192字节(48ms/帧)。
#ifndef TALK_CODEC_H
#define TALK_CODEC_H

#include <Arduino.h>

// IMA-ADPCM 步长表(89项标准表)
static const int talkAdpcmStepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34,
    37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
    544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
    1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767
};

// 索引调整表(低3位查)
static const int talkAdpcmIndexTable[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

struct TalkAdpcmState {
    int predictor;   // 上一样本预测值
    int stepIndex;   // 步长索引0..88
};

// 编码: pcm[inSamples] → adpcm[outBytes], inSamples必须为偶数(2样本/字节)
// state 跨帧保留(连续流), PTT开始/模式切换时调用 talkAdpcmReset 重置。
inline void talkAdpcmReset(TalkAdpcmState& st) {
    st.predictor = 0;
    st.stepIndex = 0;
}

inline size_t talkAdpcmEncode(TalkAdpcmState& st, const int16_t* pcm,
                              int inSamples, uint8_t* adpcm) {
    int out = 0;
    for (int i = 0; i + 1 < inSamples; i += 2) {
        uint8_t byte = 0;
        for (int k = 0; k < 2; k++) {
            int sample = pcm[i + k];
            int diff = sample - st.predictor;
            int code = 0;
            if (diff < 0) { code = 8; diff = -diff; }
            int step = talkAdpcmStepTable[st.stepIndex];
            int vpdiff = step >> 3;
            if (diff >= step)     { code |= 4; diff -= step; vpdiff += step; }
            if (diff >= step >> 1){ code |= 2; diff -= step >> 1; vpdiff += step >> 1; }
            if (diff >= step >> 2){ code |= 1; vpdiff += step >> 2; }
            if (code & 8) st.predictor -= vpdiff;
            else          st.predictor += vpdiff;
            if (st.predictor > 32767) st.predictor = 32767;
            else if (st.predictor < -32768) st.predictor = -32768;
            st.stepIndex += talkAdpcmIndexTable[code & 0x07];
            if (st.stepIndex < 0) st.stepIndex = 0;
            else if (st.stepIndex > 88) st.stepIndex = 88;
            if (k == 0) byte = (uint8_t)(code & 0x0F);
            else        byte |= (uint8_t)((code & 0x0F) << 4);
        }
        adpcm[out++] = byte;
    }
    return (size_t)out;
}

// 解码: adpcm[inBytes] → pcm[outSamples], outSamples=inBytes*2
inline size_t talkAdpcmDecode(TalkAdpcmState& st, const uint8_t* adpcm,
                              int inBytes, int16_t* pcm) {
    int out = 0;
    for (int i = 0; i < inBytes; i++) {
        uint8_t byte = adpcm[i];
        for (int k = 0; k < 2; k++) {
            int code = (k == 0) ? (byte & 0x0F) : ((byte >> 4) & 0x0F);
            int step = talkAdpcmStepTable[st.stepIndex];
            int vpdiff = step >> 3;
            if (code & 4) vpdiff += step;
            if (code & 2) vpdiff += step >> 1;
            if (code & 1) vpdiff += step >> 2;
            if (code & 8) st.predictor -= vpdiff;
            else          st.predictor += vpdiff;
            if (st.predictor > 32767) st.predictor = 32767;
            else if (st.predictor < -32768) st.predictor = -32768;
            pcm[out++] = (int16_t)st.predictor;
            st.stepIndex += talkAdpcmIndexTable[code & 0x07];
            if (st.stepIndex < 0) st.stepIndex = 0;
            else if (st.stepIndex > 88) st.stepIndex = 88;
        }
    }
    return (size_t)out;
}

// 8kHz↔16kHz 简易重采样(线性, 整形; 语音够用)
// down: 16k→8k 取偶样本(调用前已做抗混叠可忽略, 对讲场景可接受)
inline void talkDownsample2x(const int16_t* in, int inSamples, int16_t* out) {
    for (int i = 0; i < inSamples / 2; i++) {
        int32_t s = (int32_t)in[2 * i] + (int32_t)in[2 * i + 1];
        out[i] = (int16_t)(s / 2);
    }
}
// up: 8k→16k 线性插值
inline void talkUpsample2x(const int16_t* in, int inSamples, int16_t* out) {
    for (int i = 0; i < inSamples; i++) {
        out[2 * i] = in[i];
        int16_t nxt = (i + 1 < inSamples) ? in[i + 1] : in[i];
        out[2 * i + 1] = (int16_t)(((int32_t)in[i] + (int32_t)nxt) / 2);
    }
}

#endif
