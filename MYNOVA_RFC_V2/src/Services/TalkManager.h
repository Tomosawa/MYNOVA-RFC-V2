/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// TalkManager.h - 无线对讲通讯管理器(单例, 独立任务跑Core1)
//   职责: ESP-NOW 初始化/配对/收发队列/语音分发/文字可靠传输。
//   音频编解码与I2S双工由 AudioPlayer 的 TALK 引擎承担, 本模块只搬运帧。
//
// 线程模型:
//   - ESP-NOW 收发回调(系统WiFi任务上下文): 只做拷贝+入队, 不阻塞。
//   - Talk任务(Core1): 配对超时/重传/心跳/BEACON扫描聚合。
//   - UI任务: 读快照(getPeerList/getChat等), 发命令(scan/connect/sendText...)。
#ifndef TALK_MANAGER_H
#define TALK_MANAGER_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "../Proto/TalkProto.h"

// 扫描到的对端设备
struct TalkPeerInfo {
    uint8_t mac[6];
    char    name[TALK_DEVNAME_MAX + 1];
    int8_t  rssi;          // ESP-NOW回调不带RSSI, 由BEACON载荷/或promiscuous补充; 默认-60
    bool    busy;          // 对端通话中
    unsigned long lastSeenMs;
    bool    valid;
};

// 聊天消息(环形历史, 新→旧)
struct TalkChatMsg {
    char    text[TALK_TEXT_FRAG_BYTES * TALK_TEXT_MAX_FRAGS + 1];  // 收齐后拼接
    char    peerName[TALK_DEVNAME_MAX + 1];
    bool    mine;          // true=我发的
    unsigned long timeMs;
    bool    valid;
};

// 连接状态
enum TalkLinkState {
    TALK_LINK_IDLE,       // 未初始化/射频关闭
    TALK_LINK_READY,      // ESP-NOW就绪, 未连接
    TALK_LINK_PAIRING,    // 配对请求已发, 等对端确认
    TALK_LINK_INCOMING,   // 收到配对请求, 等本机确认
    TALK_LINK_CONNECTED,  // 已连接(可语音/文字)
};

// 通话模式
enum TalkMode {
    TALK_MODE_PTT = 0,    // 半双工PTT(默认)
    TALK_MODE_FULL = 1,   // 全双工
};

// 快捷短语(中文, 存flash常量, 发送走CTRL_QUICK, 接收端按索引显示+滴声)
extern const char* TALK_QUICK_PHRASES[TALK_QUICK_COUNT];

class TalkManager {
public:
    TalkManager();
    ~TalkManager();

    // ---- 生命周期(任意任务调用, 内部串行到Talk任务) ----
    void init();                 // 创建队列/任务(开机调一次, 不开射频)
    bool begin();                // 申请STA并启动ESP-NOW(进对讲页时调)
    void end();                  // 关ESP-NOW+WiFi OFF(离对讲页时调)
    bool isReady() const { return linkStateV != TALK_LINK_IDLE; }

    // ---- 配对/UI命令(非阻塞, 入队即返) ----
    void startScan();                       // 广播BEACON并收集对端(10秒窗口由页面控制)
    void stopScan();
    bool isScanning() const { return scanningV; }
    int getPeerCount() const;
    bool getPeer(int idx, TalkPeerInfo& out) const;  // 按lastSeen倒序
    void connectPeer(const uint8_t mac[6], uint8_t wantMode);  // 发PAIR_REQ
    void acceptIncoming(bool accept);       // 对来电确认/拒绝
    void disconnect();                      // 发LEAVE并回READY
    void setBusy(bool busy);                // 通话忙闲广播(语音页进出时调)

    // ---- 来电 ----
    const char* getIncomingName() const { return incomingName; }  // 扫描页来电弹窗显示
    void getIncomingMac(uint8_t out[6]) const;

    // ---- 通话 ----
    TalkLinkState getLinkState() const { return linkStateV; }
    TalkMode getMode() const { return (TalkMode)modeV; }
    void requestMode(TalkMode m);           // 发CTRL_MODE请求对端同步切换
    bool isPeerConnected() const { return linkStateV == TALK_LINK_CONNECTED; }
    // 最近一次断链原因/事件时刻(UI掉线提示用): 仅对端LEAVE/LOST刷新时间戳,
    // 页面记录上次所见时间戳即可保证"每次掉线只提示一次"
    uint8_t getLinkDropReason() const { return linkDropReasonV; }
    unsigned long getLastDropMs() const { return lastDropMsV; }
    void getPeerName(char* out, size_t sz) const;
    void getPeerMac(uint8_t out[6]) const;
    int8_t getPeerRssi() const { return peerRssiV; }

    // ---- 语音帧搬运(供AudioPlayer TALK引擎调用, 音频任务上下文) ----
    // pushTx: 存一帧待发ADPCM(音频任务→Talk任务发ESP-NOW); pullTx供Talk任务取;
    // pushRx: 存一帧远端ADPCM(WiFi回调→音频任务); popRx供音频任务取播放。
    // TX满则丢最旧一帧(保实时); RX满则丢新帧(防播放端堆积爆音)。
    void pushTxFrame(const uint8_t in[TALK_VOICE_FRAME_BYTES], uint8_t codec, bool last);
    bool pullTxFrame(uint8_t out[TALK_VOICE_FRAME_BYTES], uint8_t& codec, bool& last);
    void pushRxFrame(const uint8_t in[TALK_VOICE_FRAME_BYTES]);
    bool popRxFrame(uint8_t out[TALK_VOICE_FRAME_BYTES]);  // AudioPlayer取播放帧
    void setPttTalking(bool talking);       // PTT按下态(语音页设置, 发BEACON busy用)
    bool hasRxVoice(unsigned long sinceMs); // sinceMs内收到过语音帧(语音页RX灯用)
    unsigned long getLastRxVoiceMs() const { return lastRxVoiceMsV; }
    void notifyVoiceSent();                 // 已发一帧(TX灯用)
    unsigned long getLastTxVoiceMs() const { return lastTxVoiceMsV; }

    // ---- 文字(UI任务调用, 非阻塞) ----
    bool sendText(const String& text);      // 分片+ACK重传(600ms×3次), 历史立即加一条mine
    // 快捷短语(CTRL_QUICK单包, 对端入历史+滴声; 比sendText更轻, 语音页7/9键用)
    bool sendQuick(uint8_t idx);
    int getChatCount() const;
    bool getChat(int idx, TalkChatMsg& out) const;  // idx=0最新
    void clearChat();
    bool hasUnread() const { return unreadV; }
    void markRead() { unreadV = false; }
    unsigned long getLastTextMs() const { return lastTextMsV; }

    // ---- 设置(NVS, TalkSetting页用) ----
    void setDeviceName(const String& name);
    String getDeviceName() const;
    void setChannel(uint8_t ch);
    uint8_t getChannel() const { return channelV; }

private:
    // ---- ESP-NOW 回调(静态, WiFi任务上下文, 只做浅拷贝) ----
    static void onRecvStatic(const esp_now_recv_info_t* info, const uint8_t* data, int len);
    // core3.x发送回调签名: wifi_tx_info_t*(含发送目标desc字段)
    static void onSendStatic(const esp_now_send_info_t* info, esp_now_send_status_t status);
    void onRecv(const uint8_t* mac, const uint8_t* data, int len, int8_t rssi);
    void handleBeacon(const uint8_t* mac, const TalkBeacon* b);
    void handlePairReq(const uint8_t* mac, const TalkPairReq* r);
    void handlePairAck(const uint8_t* mac, const TalkPairAck* a);
    void handleVoice(const uint8_t* mac, const TalkVoice* v, int len);
    void handleText(const uint8_t* mac, const TalkText* t, int len);
    void handleTextAck(const uint8_t* mac, const TalkTextAck* a);
    void handleCtrl(const uint8_t* mac, const TalkCtrl* c);
    void handleLeave(const uint8_t* mac);

    bool addPeerRaw(const uint8_t mac[6]);  // esp_now_add_peer(无加密, 当前信道)
    void sendBeaconNow();                   // 立即广播一帧BEACON
    // 统一断链清理(对端LEAVE/心跳超时/发送失败/本端主动断开共用);
    // reason=TalkLinkDrop; 清队列/弃文字待确认/复位忙闲, LOST/LEAVE刷新掉线事件
    void breakLink(uint8_t reason);
    void sendTextFrags(uint8_t msgId, uint8_t total, const String& text);  // 首发/重发整条
    void talkTaskLoop();
    static void talkTaskFunc(void* param);

    // 单例回调桥
    static TalkManager* sInst;

    // ---- 任务/队列 ----
    TaskHandle_t taskHandle = nullptr;
    QueueHandle_t rxQueue = nullptr;   // 收包队列(元素=堆拷贝, Talk任务消费后free)
    struct RxItem { uint8_t mac[6]; uint8_t data[250]; uint8_t len; int8_t rssi; };
    SemaphoreHandle_t mutex = nullptr; // 保护peers/chat/状态快照
    static const int TASK_STACK = 8192;

    // ---- 状态快照(volatile跨任务, 复杂结构持锁) ----
    volatile TalkLinkState linkStateV = TALK_LINK_IDLE;
    volatile TalkMode modeV = TALK_MODE_PTT;
    volatile bool scanningV = false;
    volatile int8_t peerRssiV = -60;
    volatile unsigned long lastRxVoiceMsV = 0;
    volatile unsigned long lastTxVoiceMsV = 0;
    volatile unsigned long lastTextMsV = 0;
    volatile bool unreadV = false;
    volatile uint8_t channelV = TALK_CH_DEFAULT;
    volatile bool pttTalkingV = false;
    volatile bool busyV = false;
    // 连接存活看门狗(双路): lastPeerRxMsV=对端最近合法帧时刻(心跳超时通道);
    // txFailCountV=单播连续发送失败数(快速通道, WiFi回调写/Talk任务读)
    volatile unsigned long lastPeerRxMsV = 0;
    volatile uint16_t txFailCountV = 0;
    volatile uint8_t linkDropReasonV = TALK_DROP_LOCAL;
    volatile unsigned long lastDropMsV = 0;

    static const int PEER_MAX = 8;
    TalkPeerInfo peers[PEER_MAX];
    TalkPeerInfo connPeer;             // 当前连接对端
    bool hasConnPeer = false;

    uint8_t incomingMac[6];            // 来电方MAC
    char incomingName[TALK_DEVNAME_MAX + 1];

    // 语音环形缓冲(音频任务↔WiFi回调跨任务, 无锁SPSC: 头尾索引volatile)
    // RX: WiFi回调push→音频任务pop播放(8帧≈192ms抖动)。
    static const int VOICE_Q = 8;
    uint8_t rxBuf[VOICE_Q][TALK_VOICE_FRAME_BYTES];
    volatile uint8_t rxHead = 0, rxTail = 0;

    // 文字重组(单条进行中) + 历史
    uint8_t reasmMsgId = 0;
    uint8_t reasmTotal = 0;
    uint8_t reasmGot = 0;
    uint8_t reasmBuf[TALK_TEXT_FRAG_BYTES * TALK_TEXT_MAX_FRAGS];
    uint8_t reasmPeer[6];
    char reasmPeerName[TALK_DEVNAME_MAX + 1];
    TalkChatMsg chat[TALK_CHAT_HIST];
    uint8_t chatHead = 0;              // 下一写入位
    uint8_t chatCountV = 0;
    uint8_t nextMsgId = 1;
    String deviceName = "MYNOVA";

    // 心跳/BEACON节流
    unsigned long lastBeaconMs = 0;
    unsigned long lastBeaconReplyMs = 0;  // 扫描互见回补BEACON节流(防A↔B乒乓风暴)
    unsigned long scanStartMs = 0;
    // 文字待确认(ACK驱动重传; 同一时刻只跟踪最新一条, 新消息覆盖旧pend)
    uint8_t pendMsgId = 0;
    uint8_t pendTotal = 0;
    String pendText;                 // 待重发全文(≤1600B堆拷贝, ACK/放弃/断开后释放)
    uint8_t pendMac[6] = {0};
    unsigned long pendNextMs = 0;    // 下次重发时刻
    uint8_t pendRetries = 0;         // 剩余重发次数
    // TX待发语音队列(音频任务push→Talk任务pull发ESP-NOW; 话路序号Talk任务维护)
    struct TxItem { uint8_t data[TALK_VOICE_FRAME_BYTES]; uint8_t codec; bool last; };
    static const int TXQ_LEN = 8;
    TxItem txQ[TXQ_LEN];
    volatile uint8_t txQHead = 0, txQTail = 0;
    uint8_t voiceSeqV = 0;        // VOICE包序号(回绕, 仅Talk任务写)
    uint16_t voicePcmSeqV = 0;    // 累计样本序号(仅Talk任务写, +384/帧)
};

extern TalkManager talkManager;

#endif
