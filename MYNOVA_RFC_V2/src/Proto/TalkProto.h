/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// TalkProto.h - 无线对讲 ESP-NOW 线协议(1字节打包, 所有固件版本共用)
//   语音为UDP式(丢包不重传, seq+抖动缓冲); 文字带msgId/分片/ACK可靠传输。
//   发送端MTU: ESP-NOW单包净荷≤250B; VOICE帧固定 8B头+192B ADPCM=200B。
#ifndef TALK_PROTO_H
#define TALK_PROTO_H

#include <Arduino.h>

// 协议版本(握手/配对时核对, 大版本不同拒绝连接)
#define TALK_PROTO_VER      1

// 默认无线信道(存NVS, 设置页可改; 配对时以发起方信道为准)
#define TALK_CH_DEFAULT     1

// 帧类型
enum TalkMsgType : uint8_t {
    TALK_BEACON    = 0x01,  // 广播心跳: 设备名/MAC/忙闲/协议版本
    TALK_PAIR_REQ  = 0x02,  // 配对请求(单播)
    TALK_PAIR_ACK  = 0x03,  // 配对应答(accept=1接受/0拒绝)
    TALK_PAIR_LEAVE= 0x04,  // 断开/离开
    TALK_VOICE     = 0x10,  // 语音帧(PTT/全双工共用)
    TALK_TEXT      = 0x20,  // 文字分片
    TALK_TEXT_ACK  = 0x21,  // 文字确认(msgId)
    TALK_CTRL      = 0x30,  // 控制(模式切换/静音/音量同步/快捷短语)
};

// 断链原因(breakLink置, UI据此区分提示文案; 仅LEAVE/LOST刷新掉线事件时间戳)
enum TalkLinkDrop : uint8_t {
    TALK_DROP_LOCAL = 0,  // 本端主动断开/关射频(不弹掉线提示)
    TALK_DROP_LEAVE = 1,  // 对端正常离开(收到LEAVE)
    TALK_DROP_LOST  = 2,  // 对端掉线(心跳超时或发送连续失败: 关机/超出距离)
};

// 语音编码(VOICE.codec)
enum TalkCodec : uint8_t {
    TALK_CODEC_ADPCM16K = 0,  // PTT默认: 16kHz mono ADPCM 4bit, 帧=192B(384样本≈24ms)
    TALK_CODEC_ADPCM8K  = 1,  // 全双工: 8kHz mono ADPCM 4bit, 帧=192B(384样本≈48ms)
    TALK_CODEC_PCM8K    = 2,  // 全双工高质备选: 8kHz PCM16(帧=192B=96样本≈12ms)
};

// 控制子类型(CTRL.sub)
enum TalkCtrlSub : uint8_t {
    TALK_CTRL_MODE      = 0x01,  // payload[0]=0 PTT/1 全双工(请求切换)
    TALK_CTRL_MUTE      = 0x02,  // payload[0]=0/1 取消/静音
    TALK_CTRL_QUICK     = 0x03,  // payload[0]=快捷短语索引(喂/收到/完毕...)
    TALK_CTRL_BUSY      = 0x04,  // payload[0]=0空闲/1忙(通话中广播给扫描方)
};

// 语音帧长(固定, 避免分片与抖动缓冲变长)
#define TALK_VOICE_FRAME_BYTES  192   // ADPCM 4bit → 384样本/帧
#define TALK_VOICE_SAMPLES      384
#define TALK_TEXT_FRAG_BYTES    200   // TEXT分片净荷(UTF-8按字节切, 显示端容错)
#define TALK_TEXT_MAX_FRAGS     8     // 单条最多分片(≈1600B, 约500汉字)
// 文字ACK超时与重传次数(600ms×3次≈1.8秒窗口, ESP-NOW局域可达)
#define TALK_TEXT_ACK_MS    600
#define TALK_TEXT_RETRIES   3
// 连接存活看门狗: 对端BEACON周期2秒, 连续3个周期(6秒)无任何合法帧→判定掉线
// (对端异常断电/超出距离时收不到LEAVE, 靠此超时兜底; 任意语音/控制帧也刷新存活)
#define TALK_LINK_TIMEOUT_MS  6000
// 单播发送连续失败上限(对端关机时ESP-NOW无802.11 ACK快速FAIL):
// PTT 16k≈42pps约0.5秒/全双工8k≈21pps约1秒, 讲话中的快速掉线检出通道
#define TALK_TX_FAIL_MAX      20
#define TALK_DEVNAME_MAX        20    // 设备名最大字节(UTF-8约6个汉字)
#define TALK_CHAT_HIST          20    // 聊天历史保留条数
#define TALK_QUICK_COUNT        4     // 快捷短语条数

// 包头(所有帧共用, #pragma pack保证无填充)
#pragma pack(push, 1)
struct TalkHdr {
    uint8_t  magic;    // 固定 0x54('T')
    uint8_t  ver;      // TALK_PROTO_VER
    uint8_t  type;     // TalkMsgType
    uint8_t  seq;      // VOICE: 包序号(回绕); TEXT: 分片序号
};
struct TalkBeacon {    // type=TALK_BEACON, Hdr后载荷
    TalkHdr  hdr;
    char     name[TALK_DEVNAME_MAX + 1];
    uint8_t  peerMac[6];   // 本机MAC(接收方可直接add_peer)
    uint8_t  busy;         // 0空闲/1通话中
    uint8_t  rsv;
};
struct TalkPairReq {   // type=TALK_PAIR_REQ
    TalkHdr  hdr;
    char     name[TALK_DEVNAME_MAX + 1];
    uint8_t  peerMac[6];
    uint8_t  wantMode;     // 0 PTT / 1 全双工(期望模式, 对端可稍后切换)
};
struct TalkPairAck {   // type=TALK_PAIR_ACK
    TalkHdr  hdr;
    uint8_t  accept;       // 1接受/0拒绝
    char     name[TALK_DEVNAME_MAX + 1];
    uint8_t  peerMac[6];
};
struct TalkVoice {     // type=TALK_VOICE, 总长 8+192=200B
    TalkHdr  hdr;          // seq=包序号
    uint8_t  codec;        // TalkCodec
    uint8_t  flags;        // bit0=PTT尾帧(松开), bit1=静音帧
    uint16_t pcmSeq;       // 累计样本序号(抖动缓冲排序/丢包隐藏用)
    uint8_t  data[TALK_VOICE_FRAME_BYTES];
};
struct TalkText {      // type=TALK_TEXT
    TalkHdr  hdr;          // seq=分片序号(0..total-1)
    uint8_t  msgId;        // 消息ID(回绕)
    uint8_t  total;        // 总分片数
    uint8_t  data[TALK_TEXT_FRAG_BYTES];
};
struct TalkTextAck {   // type=TALK_TEXT_ACK
    TalkHdr  hdr;
    uint8_t  msgId;
    uint8_t  ok;           // 1收齐/0缺片(缺片时payload为位图, 暂简化为整条重发)
};
struct TalkCtrl {      // type=TALK_CTRL
    TalkHdr  hdr;
    uint8_t  sub;          // TalkCtrlSub
    uint8_t  payload[16];
};
#pragma pack(pop)

#define TALK_MAGIC 0x54

#endif
