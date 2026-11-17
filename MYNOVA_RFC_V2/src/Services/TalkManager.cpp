/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// TalkManager.cpp - 无线对讲通讯管理器实现
//   收包回调只拷贝入队; 配对/重传/心跳在Talk任务(Core1)串行处理。
//   语音帧走无锁SPSC环形缓冲, 供AudioPlayer TALK引擎(音频任务)搬运。
#include "TalkManager.h"
#include "DataStore.h"
#include "WiFiManager.h"
#include "../Util/TaskConfig.h"
#include <esp_mac.h>        // esp_read_mac/ESP_MAC_WIFI_STA
#include <Preferences.h>    // init读NVS(设备名/信道/模式)
#include "../Util/Log.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "TALK"

extern DataStore dataStore;
extern WiFiManager wifiManager;

TalkManager talkManager;
TalkManager* TalkManager::sInst = nullptr;

const char* TALK_QUICK_PHRASES[TALK_QUICK_COUNT] = {
    "喂,能听到吗?",
    "收到!",
    "完毕,通话结束",
    "请讲",
};

// NVS键(与DataStore同命名空间RadioData, 避免新增命名空间开销)
#define TALK_NVS_NAME "TALK_NAME"
#define TALK_NVS_CH   "TALK_CH"

TalkManager::TalkManager() {
    mutex = xSemaphoreCreateMutex();
    memset(peers, 0, sizeof(peers));
    memset(&connPeer, 0, sizeof(connPeer));
    memset(chat, 0, sizeof(chat));
    memset(reasmBuf, 0, sizeof(reasmBuf));
    sInst = this;
}

TalkManager::~TalkManager() {
    if (taskHandle) vTaskDelete(taskHandle);
    if (rxQueue) vQueueDelete(rxQueue);
    if (mutex) vSemaphoreDelete(mutex);
    sInst = nullptr;
}

void TalkManager::init() {
    if (taskHandle) return;
    // 读NVS(RadioData命名空间, 与DataStore同库; 失败用默认)
    deviceName = "MYNOVA-" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFF), HEX);
    deviceName.toUpperCase();
    channelV = TALK_CH_DEFAULT;
    uint8_t savedMode = 0;
    Preferences pf;
    if (pf.begin("RadioData", true)) {
        deviceName = pf.getString(TALK_NVS_NAME, deviceName);
        // 按字节截断(与setDeviceName一致, 防NVS残留超长)
        if (deviceName.length() > TALK_DEVNAME_MAX)
            deviceName = deviceName.substring(0, TALK_DEVNAME_MAX);
        int ch = pf.getInt(TALK_NVS_CH, channelV);
        if (ch >= 1 && ch <= 13) channelV = (uint8_t)ch;
        savedMode = (uint8_t)pf.getInt("TALK_MODE", 0);
        pf.end();
    }
    modeV = (savedMode == 1) ? TALK_MODE_FULL : TALK_MODE_PTT;
    rxQueue = xQueueCreate(16, sizeof(RxItem));
    // 钉Core1(业务核,维持), 优先级3=硬时序业务(规划§4.3, 原2)
    xTaskCreatePinnedToCore(talkTaskFunc, TASK_TALK_NAME, TASK_TALK_STACK, this,
                            TASK_PRIO_HW, &taskHandle, TASK_CORE_APP);
    LOG_I("TalkManager: 任务已启动(Core1, 射频未开, 名%s CH%d 模式%s)",
          deviceName.c_str(), (int)channelV,
          modeV == TALK_MODE_PTT ? "PTT" : "FULL");
}

bool TalkManager::begin() {
    if (linkStateV != TALK_LINK_IDLE) return true;
    if (!rxQueue || !wifiManager.beginTalk(channelV)) {
        LOG_W("TalkManager: WiFi暂不可用");
        return false;
    }
    if (esp_now_init() != ESP_OK) {
        wifiManager.endTalk();
        LOG_E("TalkManager: esp_now_init失败");
        return false;
    }

    uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    if (esp_now_register_recv_cb(onRecvStatic) != ESP_OK ||
        esp_now_register_send_cb(onSendStatic) != ESP_OK || !addPeerRaw(broadcast)) {
        esp_now_deinit();
        wifiManager.endTalk();
        return false;
    }
    linkStateV = TALK_LINK_READY;
    LOG_I("TalkManager: 就绪(信道%d, 名%s)", (int)channelV, deviceName.c_str());
    return true;
}

void TalkManager::end() {
    if (linkStateV == TALK_LINK_IDLE) return;
    // 发LEAVE(尽力, 不等确认)
    if (hasConnPeer) {
        TalkHdr h = {TALK_MAGIC, TALK_PROTO_VER, TALK_PAIR_LEAVE, 0};
        esp_now_send(connPeer.mac, (uint8_t*)&h, sizeof(h));
    }
    // 统一断链清理(队列/待确认/忙闲/看门狗计数); 本端主动关射频不产生掉线事件
    breakLink(TALK_DROP_LOCAL);
    // 关闭ESP-NOW回调前先禁止新的语音/文字状态，避免UI返回期间回调继续写入队列
    linkStateV = TALK_LINK_IDLE;
    scanningV = false;
    if (esp_now_deinit() != ESP_OK) {
        LOG_W("TalkManager: esp_now_deinit返回非OK");
    }
    wifiManager.endTalk();
    voiceSeqV = 0;
    voicePcmSeqV = 0;
    LOG_I("TalkManager: 对讲已关闭");
}

// ================= 配对/UI命令 =================
void TalkManager::startScan() {
    if (linkStateV == TALK_LINK_IDLE) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    memset(peers, 0, sizeof(peers));
    xSemaphoreGive(mutex);
    scanningV = true;
    scanStartMs = millis();
    sendBeaconNow();   // 立即一帧, 对端收到后会回BEACON(见handleBeacon回补)
    LOG_I("TalkManager: 开始扫描");
}

void TalkManager::stopScan() {
    scanningV = false;
    LOG_I("TalkManager: 停止扫描");
}

int TalkManager::getPeerCount() const {
    int n = 0;
    // 快照读(调用方UI任务, 轻量持锁)
    xSemaphoreTake(((TalkManager*)this)->mutex, portMAX_DELAY);
    for (int i = 0; i < PEER_MAX; i++) if (peers[i].valid) n++;
    xSemaphoreGive(((TalkManager*)this)->mutex);
    return n;
}

bool TalkManager::getPeer(int idx, TalkPeerInfo& out) const {
    bool ok = false;
    xSemaphoreTake(((TalkManager*)this)->mutex, portMAX_DELAY);
    // 按lastSeen倒序
    int best = -1;
    unsigned long bestMs = 0;
    // 简选: 收集有效项按时间排(PEER_MAX=8, O(n²)可接受)
    TalkPeerInfo tmp[PEER_MAX];
    int n = 0;
    for (int i = 0; i < PEER_MAX; i++) if (peers[i].valid) tmp[n++] = peers[i];
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (tmp[j].lastSeenMs > tmp[i].lastSeenMs) {
                TalkPeerInfo t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t;
            }
    if (idx >= 0 && idx < n) { out = tmp[idx]; ok = true; }
    (void)best; (void)bestMs;
    xSemaphoreGive(((TalkManager*)this)->mutex);
    return ok;
}

void TalkManager::connectPeer(const uint8_t mac[6], uint8_t wantMode) {
    if (linkStateV != TALK_LINK_READY) return;
    addPeerRaw(mac);
    TalkPairReq r;
    r.hdr.magic = TALK_MAGIC; r.hdr.ver = TALK_PROTO_VER;
    r.hdr.type = TALK_PAIR_REQ; r.hdr.seq = 0;
    strncpy(r.name, deviceName.c_str(), TALK_DEVNAME_MAX);
    r.name[TALK_DEVNAME_MAX] = '\0';
    esp_read_mac(r.peerMac, ESP_MAC_WIFI_STA);
    r.wantMode = wantMode;
    if (esp_now_send(mac, (uint8_t*)&r, sizeof(r)) == ESP_OK) {
        memcpy(incomingMac, mac, 6);
        linkStateV = TALK_LINK_PAIRING;
        LOG_I("TalkManager: 配对请求已发, 等对端确认");
    }
}

void TalkManager::acceptIncoming(bool accept) {
    if (linkStateV != TALK_LINK_INCOMING) return;
    TalkPairAck a;
    a.hdr.magic = TALK_MAGIC; a.hdr.ver = TALK_PROTO_VER;
    a.hdr.type = TALK_PAIR_ACK; a.hdr.seq = 0;
    a.accept = accept ? 1 : 0;
    strncpy(a.name, deviceName.c_str(), TALK_DEVNAME_MAX);
    a.name[TALK_DEVNAME_MAX] = '\0';
    esp_read_mac(a.peerMac, ESP_MAC_WIFI_STA);
    addPeerRaw(incomingMac);
    esp_now_send(incomingMac, (uint8_t*)&a, sizeof(a));
    if (accept) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        memcpy(connPeer.mac, incomingMac, 6);
        strncpy(connPeer.name, incomingName, TALK_DEVNAME_MAX);
        connPeer.lastSeenMs = millis();
        connPeer.valid = true;
        hasConnPeer = true;
        xSemaphoreGive(mutex);
        linkStateV = TALK_LINK_CONNECTED;
        // 连接建立: 存活看门狗起算, 清失败计数与旧掉线原因
        lastPeerRxMsV = millis();
        txFailCountV = 0;
        linkDropReasonV = TALK_DROP_LOCAL;
        LOG_I("TalkManager: 已连接 %s", incomingName);
    } else {
        linkStateV = TALK_LINK_READY;
        LOG_I("TalkManager: 已拒绝来电");
    }
}

void TalkManager::disconnect() {
    if (linkStateV != TALK_LINK_CONNECTED) return;
    TalkHdr h = {TALK_MAGIC, TALK_PROTO_VER, TALK_PAIR_LEAVE, 0};
    if (hasConnPeer) esp_now_send(connPeer.mac, (uint8_t*)&h, sizeof(h));
    breakLink(TALK_DROP_LOCAL);   // 本端主动断开, 不产生掉线事件
}

// 统一断链清理: 对端LEAVE/心跳超时/发送失败/本端主动断开共用。
// 可在Talk任务(看门狗/handleLeave)或UI任务(disconnect/end)上下文调用。
void TalkManager::breakLink(uint8_t reason) {
    bool wasLinked = hasConnPeer || linkStateV == TALK_LINK_CONNECTED;
    xSemaphoreTake(mutex, portMAX_DELAY);
    hasConnPeer = false;
    connPeer.valid = false;
    // 弃文字待确认(重连后由用户重发, 防旧pend在下次连接误重发)
    pendRetries = 0;
    pendText = String();
    xSemaphoreGive(mutex);
    // 清语音队列防旧帧残留; 复位PTT/忙闲与看门狗计数(volatile, 无需持锁)
    txQHead = txQTail = 0;
    rxHead = rxTail = 0;
    pttTalkingV = false;
    busyV = false;
    lastPeerRxMsV = 0;
    txFailCountV = 0;
    if (linkStateV != TALK_LINK_IDLE) linkStateV = TALK_LINK_READY;
    if (reason != TALK_DROP_LOCAL) {
        linkDropReasonV = reason;
        lastDropMsV = millis();
    }
    if (wasLinked) {
        if (reason == TALK_DROP_LEAVE)
            LOG_I("TalkManager: 对端离开");
        else if (reason == TALK_DROP_LOST)
            LOG_W("TalkManager: 对端掉线(关机/超出距离)");
        else
            LOG_I("TalkManager: 已断开(本端)");
    }
}

void TalkManager::setBusy(bool busy) {
    busyV = busy;
    if (linkStateV == TALK_LINK_IDLE) return;
    sendBeaconNow();   // 忙闲变化立即广播一次(扫描方列表实时)
}

void TalkManager::requestMode(TalkMode m) {
    modeV = m;   // 本端先切
    // 持久化默认模式(设置页saveAndReturn不再发CTRL, 此处统一落盘; 语音页4键切
    // 也同步默认, 下次开机/进对讲即该模式, 与设置页显示一致)
    {
        DataStore::StorageGuard storage(dataStore);
        Preferences pf;
        if (storage && pf.begin(DataStore::SettingsNamespace, false)) {
            pf.putInt("TALK_MODE", (int)m);
            pf.end();
        }
    }
    if (linkStateV == TALK_LINK_CONNECTED && hasConnPeer) {
        TalkCtrl c;
        c.hdr.magic = TALK_MAGIC; c.hdr.ver = TALK_PROTO_VER;
        c.hdr.type = TALK_CTRL; c.hdr.seq = 0;
        c.sub = TALK_CTRL_MODE;
        memset(c.payload, 0, sizeof(c.payload));
        c.payload[0] = (uint8_t)m;
        esp_now_send(connPeer.mac, (uint8_t*)&c, sizeof(c));
    }
    LOG_I("TalkManager: 模式→%s(已持久化)", m == TALK_MODE_PTT ? "PTT" : "全双工");
}

void TalkManager::getPeerName(char* out, size_t sz) const {
    if (!out || sz == 0) return;
    xSemaphoreTake(((TalkManager*)this)->mutex, portMAX_DELAY);
    strncpy(out, hasConnPeer ? connPeer.name : "-", sz - 1);
    out[sz - 1] = '\0';
    xSemaphoreGive(((TalkManager*)this)->mutex);
}

void TalkManager::getPeerMac(uint8_t out[6]) const {
    xSemaphoreTake(((TalkManager*)this)->mutex, portMAX_DELAY);
    if (hasConnPeer) memcpy(out, connPeer.mac, 6);
    else memset(out, 0, 6);
    xSemaphoreGive(((TalkManager*)this)->mutex);
}

void TalkManager::getIncomingMac(uint8_t out[6]) const {
    memcpy(out, ((TalkManager*)this)->incomingMac, 6);   // 配对期UI任务读, Talk任务写同名
}

// ================= 语音帧搬运 =================
// TX路径: 音频任务talkProcessMic编码后pushTxFrame→Talk任务pullTxFrame组包发ESP-NOW。
// (不在音频任务直接esp_now_send: WiFi任务上下文回调重入风险+音频任务时序抖动。)
// 并发说明: push(音频任务Core0)与pull(Talk任务Core1)为SPSC; 头/尾索引为volatile
// uint8_t, 单字节读写原子。push先写数据槽再推进head, pull先读数据槽再推进tail,
// 满/空判断各只依赖对方索引的一次快照 —— ESP32上无撕裂, 无需加锁。
void TalkManager::pushTxFrame(const uint8_t in[TALK_VOICE_FRAME_BYTES], uint8_t codec, bool last) {
    uint8_t tailSnap = txQTail;   // 一次快照(防编译器/任务切换间重复读取)
    uint8_t nh = (txQHead + 1) % TXQ_LEN;
    if (nh == tailSnap) {
        // 满则丢最旧一帧(保实时, 对讲延迟>200ms即不可用)
        txQTail = (txQTail + 1) % TXQ_LEN;
    }
    memcpy(txQ[txQHead].data, in, TALK_VOICE_FRAME_BYTES);
    txQ[txQHead].codec = codec;
    txQ[txQHead].last = last;
    txQHead = nh;
}

bool TalkManager::pullTxFrame(uint8_t out[TALK_VOICE_FRAME_BYTES], uint8_t& codec, bool& last) {
    uint8_t headSnap = txQHead;   // 一次快照
    if (headSnap == txQTail) return false;
    memcpy(out, txQ[txQTail].data, TALK_VOICE_FRAME_BYTES);
    codec = txQ[txQTail].codec;
    last = txQ[txQTail].last;
    txQTail = (txQTail + 1) % TXQ_LEN;
    return true;
}

void TalkManager::pushRxFrame(const uint8_t in[TALK_VOICE_FRAME_BYTES]) {
    uint8_t tailSnap = rxTail;    // 一次快照(WiFi回调上下文, 与音频任务pop并发)
    uint8_t nh = (rxHead + 1) % VOICE_Q;
    if (nh == tailSnap) return;   // 满则丢新帧(抖动缓冲满=播放端太慢, 保连续)
    memcpy(rxBuf[rxHead], in, TALK_VOICE_FRAME_BYTES);
    rxHead = nh;
    lastRxVoiceMsV = millis();
}

bool TalkManager::popRxFrame(uint8_t out[TALK_VOICE_FRAME_BYTES]) {
    uint8_t headSnap = rxHead;    // 一次快照
    if (headSnap == rxTail) return false;
    memcpy(out, rxBuf[rxTail], TALK_VOICE_FRAME_BYTES);
    rxTail = (rxTail + 1) % VOICE_Q;
    return true;
}

void TalkManager::setPttTalking(bool talking) {
    pttTalkingV = talking;
}

bool TalkManager::hasRxVoice(unsigned long sinceMs) {
    return (millis() - lastRxVoiceMsV) < sinceMs;
}

void TalkManager::notifyVoiceSent() {
    lastTxVoiceMsV = millis();
}

// ================= 文字 =================
static void talkPushChat(TalkChatMsg* chat, uint8_t& head, uint8_t& cnt,
                         const char* text, const char* peer, bool mine) {
    TalkChatMsg& m = chat[head];
    strncpy(m.text, text, sizeof(m.text) - 1);
    m.text[sizeof(m.text) - 1] = '\0';
    strncpy(m.peerName, peer, sizeof(m.peerName) - 1);
    m.peerName[sizeof(m.peerName) - 1] = '\0';
    m.mine = mine;
    m.timeMs = millis();
    m.valid = true;
    head = (head + 1) % TALK_CHAT_HIST;
    if (cnt < TALK_CHAT_HIST) cnt++;
}

bool TalkManager::sendText(const String& text) {
    if (linkStateV != TALK_LINK_CONNECTED || !hasConnPeer) return false;
    if (text.length() == 0) return false;
    uint8_t total = (text.length() + TALK_TEXT_FRAG_BYTES - 1) / TALK_TEXT_FRAG_BYTES;
    if (total == 0) total = 1;
    if (total > TALK_TEXT_MAX_FRAGS) return false;   // 超长拒绝(UI截断提示)
    uint8_t msgId = nextMsgId++;
    if (nextMsgId == 0) nextMsgId = 1;
    {   // 登记待确认(ACK驱动重传, 见talkTaskLoop; 发送中link断开则丢弃)
        xSemaphoreTake(mutex, portMAX_DELAY);
        pendMsgId = msgId;
        pendTotal = total;
        pendText = text;   // String拷贝(≤1600B, 堆分配一次, ACK后释放)
        pendNextMs = millis() + TALK_TEXT_ACK_MS;
        pendRetries = TALK_TEXT_RETRIES;
        memcpy(pendMac, connPeer.mac, 6);
        xSemaphoreGive(mutex);
        sendTextFrags(msgId, total, text);   // 首发(分片间隔防ESP-NOW队列溢出)
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    talkPushChat(chat, chatHead, chatCountV, text.c_str(), "我", true);
    xSemaphoreGive(mutex);
    lastTextMsV = millis();
    LOG_I("TalkManager: 文字已发 msgId=%d 分片=%d(等ACK)", (int)msgId, (int)total);
    return true;
}

// 首发/重发整条文字(调用方保证已连接; UI任务或Talk任务上下文均可, esp_now_send线程安全)
void TalkManager::sendTextFrags(uint8_t msgId, uint8_t total, const String& text) {
    uint8_t mac[6];
    xSemaphoreTake(mutex, portMAX_DELAY);
    memcpy(mac, hasConnPeer ? connPeer.mac : pendMac, 6);
    bool ok = hasConnPeer && (linkStateV == TALK_LINK_CONNECTED);
    xSemaphoreGive(mutex);
    if (!ok) return;
    for (uint8_t s = 0; s < total; s++) {
        TalkText t;
        t.hdr.magic = TALK_MAGIC; t.hdr.ver = TALK_PROTO_VER;
        t.hdr.type = TALK_TEXT; t.hdr.seq = s;
        t.msgId = msgId; t.total = total;
        memset(t.data, 0, sizeof(t.data));
        size_t off = (size_t)s * TALK_TEXT_FRAG_BYTES;
        size_t n = text.length() - off;
        if (n > TALK_TEXT_FRAG_BYTES) n = TALK_TEXT_FRAG_BYTES;
        memcpy(t.data, text.c_str() + off, n);
        size_t pktLen = sizeof(TalkHdr) + 2 + n;
        esp_now_send(mac, (uint8_t*)&t, pktLen);
        vTaskDelay(pdMS_TO_TICKS(15));   // 分片间隔, 防ESP-NOW队列溢出
    }
}

int TalkManager::getChatCount() const {
    return chatCountV;
}

// 快捷短语: CTRL_QUICK单包(20B内), 对端入聊天历史+滴声由其页面处理。
// 本端同样入一条mine历史(与sendText一致的"我:短语"体验)。
bool TalkManager::sendQuick(uint8_t idx) {
    if (idx >= TALK_QUICK_COUNT) return false;
    if (linkStateV != TALK_LINK_CONNECTED || !hasConnPeer) return false;
    TalkCtrl c;
    c.hdr.magic = TALK_MAGIC; c.hdr.ver = TALK_PROTO_VER;
    c.hdr.type = TALK_CTRL; c.hdr.seq = 0;
    c.sub = TALK_CTRL_QUICK;
    memset(c.payload, 0, sizeof(c.payload));
    c.payload[0] = idx;
    if (esp_now_send(connPeer.mac, (uint8_t*)&c, sizeof(c)) != ESP_OK) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    talkPushChat(chat, chatHead, chatCountV, TALK_QUICK_PHRASES[idx], "我", true);
    xSemaphoreGive(mutex);
    lastTextMsV = millis();   // 触发聊天页刷新(本端历史即时可见)
    LOG_I("TalkManager: 快捷短语已发 idx=%d", (int)idx);
    return true;
}

bool TalkManager::getChat(int idx, TalkChatMsg& out) const {
    // idx=0最新
    xSemaphoreTake(((TalkManager*)this)->mutex, portMAX_DELAY);
    bool ok = false;
    if (idx >= 0 && idx < chatCountV) {
        int pos = ((TalkManager*)this)->chatHead - 1 - idx;
        while (pos < 0) pos += TALK_CHAT_HIST;
        out = ((TalkManager*)this)->chat[pos % TALK_CHAT_HIST];
        ok = true;
    }
    xSemaphoreGive(((TalkManager*)this)->mutex);
    return ok;
}

void TalkManager::clearChat() {
    xSemaphoreTake(mutex, portMAX_DELAY);
    memset(chat, 0, sizeof(chat));
    chatHead = 0; chatCountV = 0;
    xSemaphoreGive(mutex);
}

void TalkManager::setDeviceName(const String& name) {
    String n = name;
    n.trim();
    if (n.length() == 0) return;
    // 按字节截断到TALK_DEVNAME_MAX(UTF-8尾部可能截断, 显示端容错)
    if (n.length() > TALK_DEVNAME_MAX) n = n.substring(0, TALK_DEVNAME_MAX);
    deviceName = n;
    unreadV = false;
}

String TalkManager::getDeviceName() const {
    return deviceName;
}

void TalkManager::setChannel(uint8_t ch) {
    if (ch < 1 || ch > 13) return;
    channelV = ch;
}

// ================= ESP-NOW 回调 =================
void TalkManager::onRecvStatic(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (!sInst || !info || !data || len <= 0) return;
    if (len > 250) len = 250;
    RxItem it;
    memcpy(it.mac, info->src_addr, 6);
    memcpy(it.data, data, len);
    it.len = (uint8_t)len;
    // 收包自带RSSI(语音页信号强度用); 有效RSSI必为负, 0=无效标记
    it.rssi = (info->rx_ctrl && info->rx_ctrl->rssi < 0) ? (int8_t)info->rx_ctrl->rssi : 0;
    // WiFi任务上下文: 0等待入队, 满则丢(语音允许丢)
    xQueueSend(sInst->rxQueue, &it, 0);
}

void TalkManager::onSendStatic(const esp_now_send_info_t* info, esp_now_send_status_t status) {
    (void)info;
    // 掉线快速通道: 仅统计连接态发送结果。连接态单播出口(语音/文字/CTRL/ACK)
    // 目标均为对端; 广播BEACON无802.11 ACK, 恒SUCCESS不会误清零失败统计节奏。
    // WiFi任务上下文: 只写volatile, 不持锁/不打日志/不发ESP-NOW。
    if (!sInst || sInst->linkStateV != TALK_LINK_CONNECTED) return;
    if (status == ESP_NOW_SEND_SUCCESS) {
        sInst->txFailCountV = 0;
    } else if (sInst->txFailCountV < 0xFFFF) {
        sInst->txFailCountV++;
    }
}

void TalkManager::onRecv(const uint8_t* mac, const uint8_t* data, int len, int8_t rssi) {
    // 更新已连接对端RSSI(语音页信号强度显示; BEACON心跳保证静默期也持续刷新)
    if (rssi < 0 && hasConnPeer && memcmp(mac, connPeer.mac, 6) == 0)
        peerRssiV = rssi;
    if (len < (int)sizeof(TalkHdr)) return;
    const TalkHdr* h = (const TalkHdr*)data;
    if (h->magic != TALK_MAGIC || h->ver != TALK_PROTO_VER) return;
    // 存活看门狗: 已连接对端任意合法帧(BEACON/VOICE/TEXT/ACK/CTRL)都刷新计时,
    // 通话中的语音流本身即心跳; 同时清零发送失败计数(双向可达即自愈)
    if (linkStateV == TALK_LINK_CONNECTED && hasConnPeer &&
        memcmp(mac, connPeer.mac, 6) == 0) {
        lastPeerRxMsV = millis();
        txFailCountV = 0;
    }
    switch (h->type) {
        case TALK_BEACON:   if (len >= (int)sizeof(TalkBeacon)) handleBeacon(mac, (const TalkBeacon*)data); break;
        case TALK_PAIR_REQ: if (len >= (int)sizeof(TalkPairReq)) handlePairReq(mac, (const TalkPairReq*)data); break;
        case TALK_PAIR_ACK: if (len >= (int)sizeof(TalkPairAck)) handlePairAck(mac, (const TalkPairAck*)data); break;
        case TALK_PAIR_LEAVE: handleLeave(mac); break;
        case TALK_VOICE:    if (len >= (int)sizeof(TalkVoice)) handleVoice(mac, (const TalkVoice*)data, len); break;
        case TALK_TEXT:     handleText(mac, (const TalkText*)data, len); break;
        case TALK_TEXT_ACK: if (len >= (int)sizeof(TalkTextAck)) handleTextAck(mac, (const TalkTextAck*)data); break;
        case TALK_CTRL:     if (len >= (int)sizeof(TalkCtrl)) handleCtrl(mac, (const TalkCtrl*)data); break;
        default: break;
    }
}

void TalkManager::handleBeacon(const uint8_t* mac, const TalkBeacon* b) {
    // 只在扫描期收录(平时也更新已连接对端的busy)
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (hasConnPeer && memcmp(mac, connPeer.mac, 6) == 0) {
        connPeer.busy = b->busy;
        connPeer.lastSeenMs = millis();
        xSemaphoreGive(mutex);
        return;
    }
    if (!scanningV) { xSemaphoreGive(mutex); return; }
    // 找空位或同MAC更新
    int slot = -1;
    for (int i = 0; i < PEER_MAX; i++) {
        if (peers[i].valid && memcmp(peers[i].mac, mac, 6) == 0) { slot = i; break; }
    }
    if (slot < 0) {
        for (int i = 0; i < PEER_MAX; i++) {
            if (!peers[i].valid) { slot = i; break; }
        }
        // 满则替最旧
        if (slot < 0) {
            slot = 0;
            for (int i = 1; i < PEER_MAX; i++)
                if (peers[i].lastSeenMs < peers[slot].lastSeenMs) slot = i;
        }
        memcpy(peers[slot].mac, mac, 6);
        peers[slot].valid = true;
    }
    strncpy(peers[slot].name, b->name, TALK_DEVNAME_MAX);
    peers[slot].name[TALK_DEVNAME_MAX] = '\0';
    peers[slot].busy = b->busy;
    peers[slot].rssi = -60;
    peers[slot].lastSeenMs = millis();
    xSemaphoreGive(mutex);
    // 回补一帧BEACON(让对端扫描列表也能看到我, 穿透单向发现)。
    // 节流: 两台设备互扫时, 每收一帧都回会形成A↔B乒乓风暴(打满RX队列饿死语音),
    // 限到每500ms最多回一帧(与扫描期心跳同拍, 互相发现仍然秒级完成)。
    if (millis() - lastBeaconReplyMs > 500) {
        lastBeaconReplyMs = millis();
        sendBeaconNow();
    }
}

void TalkManager::handlePairReq(const uint8_t* mac, const TalkPairReq* r) {
    if (linkStateV != TALK_LINK_READY) {
        // 忙则回拒绝
        TalkPairAck a;
        a.hdr.magic = TALK_MAGIC; a.hdr.ver = TALK_PROTO_VER;
        a.hdr.type = TALK_PAIR_ACK; a.hdr.seq = 0;
        a.accept = 0;
        strncpy(a.name, deviceName.c_str(), TALK_DEVNAME_MAX);
        esp_read_mac(a.peerMac, ESP_MAC_WIFI_STA);
        addPeerRaw(mac);
        esp_now_send(mac, (uint8_t*)&a, sizeof(a));
        return;
    }
    memcpy(incomingMac, mac, 6);
    strncpy(incomingName, r->name, TALK_DEVNAME_MAX);
    incomingName[TALK_DEVNAME_MAX] = '\0';
    modeV = (r->wantMode == 1) ? TALK_MODE_FULL : TALK_MODE_PTT;
    linkStateV = TALK_LINK_INCOMING;
    LOG_I("TalkManager: 来电 %s, 等确认", incomingName);
}

void TalkManager::handlePairAck(const uint8_t* mac, const TalkPairAck* a) {
    if (linkStateV != TALK_LINK_PAIRING) return;
    if (memcmp(mac, incomingMac, 6) != 0) return;
    if (a->accept) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        memcpy(connPeer.mac, mac, 6);
        strncpy(connPeer.name, a->name, TALK_DEVNAME_MAX);
        connPeer.lastSeenMs = millis();
        connPeer.valid = true;
        hasConnPeer = true;
        xSemaphoreGive(mutex);
        linkStateV = TALK_LINK_CONNECTED;
        // 连接建立: 存活看门狗起算, 清失败计数与旧掉线原因
        lastPeerRxMsV = millis();
        txFailCountV = 0;
        linkDropReasonV = TALK_DROP_LOCAL;
        LOG_I("TalkManager: 对端接受 %s", a->name);
    } else {
        linkStateV = TALK_LINK_READY;
        LOG_W("TalkManager: 对端拒绝");
    }
}

void TalkManager::handleVoice(const uint8_t* mac, const TalkVoice* v, int len) {
    (void)len;
    if (linkStateV != TALK_LINK_CONNECTED || !hasConnPeer) return;
    if (memcmp(mac, connPeer.mac, 6) != 0) return;   // 只收对端语音
    pushRxFrame(v->data);
    // PTT尾帧可用于页面RX灯熄灭(此处只记时间, 页面按超时判)
}

void TalkManager::handleText(const uint8_t* mac, const TalkText* t, int len) {
    if (linkStateV != TALK_LINK_CONNECTED || !hasConnPeer) return;
    if (memcmp(mac, connPeer.mac, 6) != 0) return;
    size_t hdrOff = sizeof(TalkHdr) + 2;
    if (len < (int)hdrOff) return;
    size_t fragLen = (size_t)len - hdrOff;
    if (fragLen > TALK_TEXT_FRAG_BYTES) fragLen = TALK_TEXT_FRAG_BYTES;
    // 新消息起重组
    if (t->msgId != reasmMsgId || t->total != reasmTotal) {
        if (t->hdr.seq != 0) return;   // 首片丢失则整条放弃(等重发)
        reasmMsgId = t->msgId;
        reasmTotal = (t->total > TALK_TEXT_MAX_FRAGS) ? TALK_TEXT_MAX_FRAGS : t->total;
        reasmGot = 0;
        memcpy(reasmPeer, mac, 6);
        xSemaphoreTake(mutex, portMAX_DELAY);
        strncpy(reasmPeerName, connPeer.name, TALK_DEVNAME_MAX);
        xSemaphoreGive(mutex);
    }
    if (t->hdr.seq >= reasmTotal) return;
    // 简化: 要求按序到达(乱序到达的分片丢弃, 整条靠发送端重发补齐时自然按序)
    size_t expectOff = (size_t)reasmGot * TALK_TEXT_FRAG_BYTES;
    if ((size_t)t->hdr.seq * TALK_TEXT_FRAG_BYTES != expectOff) return;
    memcpy(reasmBuf + expectOff, t->data, fragLen);
    reasmGot++;
    if (reasmGot >= reasmTotal) {
        size_t totalLen = (reasmTotal - 1) * TALK_TEXT_FRAG_BYTES + fragLen;
        if (totalLen > sizeof(reasmBuf) - 1) totalLen = sizeof(reasmBuf) - 1;
        reasmBuf[totalLen] = '\0';
        xSemaphoreTake(mutex, portMAX_DELAY);
        talkPushChat(chat, chatHead, chatCountV, (const char*)reasmBuf, reasmPeerName, false);
        xSemaphoreGive(mutex);
        unreadV = true;
        lastTextMsV = millis();
        reasmMsgId = 0;   // 标记重组结束, 防重复
        // 回ACK
        TalkTextAck ack;
        ack.hdr.magic = TALK_MAGIC; ack.hdr.ver = TALK_PROTO_VER;
        ack.hdr.type = TALK_TEXT_ACK; ack.hdr.seq = 0;
        ack.msgId = t->msgId; ack.ok = 1;
        esp_now_send(mac, (uint8_t*)&ack, sizeof(ack));
        LOG_I("TalkManager: 收到文字(%uB)", (unsigned)totalLen);
    }
}

void TalkManager::handleTextAck(const uint8_t* mac, const TalkTextAck* a) {
    (void)mac;
    // 发送端: msgId对上即清待确认(新消息覆盖旧pend, 旧重传自然停止)
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (a->msgId == pendMsgId && pendRetries > 0) {
        pendRetries = 0;
        pendText = String();   // 释放堆拷贝
        LOG_I("TalkManager: 文字ACK msgId=%d(送达)", (int)a->msgId);
    }
    xSemaphoreGive(mutex);
}

void TalkManager::handleCtrl(const uint8_t* mac, const TalkCtrl* c) {
    if (linkStateV != TALK_LINK_CONNECTED || !hasConnPeer) return;
    if (memcmp(mac, connPeer.mac, 6) != 0) return;
    switch (c->sub) {
        case TALK_CTRL_MODE:
            modeV = (c->payload[0] == 1) ? TALK_MODE_FULL : TALK_MODE_PTT;
            LOG_I("TalkManager: 对端切模式→%s", modeV == TALK_MODE_PTT ? "PTT" : "全双工");
            break;
        case TALK_CTRL_QUICK: {
            uint8_t idx = c->payload[0];
            if (idx < TALK_QUICK_COUNT) {
                xSemaphoreTake(mutex, portMAX_DELAY);
                talkPushChat(chat, chatHead, chatCountV,
                             TALK_QUICK_PHRASES[idx], connPeer.name, false);
                xSemaphoreGive(mutex);
                unreadV = true;
                lastTextMsV = millis();
            }
            break;
        }
        default: break;
    }
}

void TalkManager::handleLeave(const uint8_t* mac) {
    if (linkStateV != TALK_LINK_CONNECTED || !hasConnPeer) return;
    if (memcmp(mac, connPeer.mac, 6) != 0) return;
    breakLink(TALK_DROP_LEAVE);
}

bool TalkManager::addPeerRaw(const uint8_t mac[6]) {
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t pi;
    memset(&pi, 0, sizeof(pi));
    memcpy(pi.peer_addr, mac, 6);
    pi.channel = 0; // Use the channel selected for the current ESP-NOW session.
    pi.encrypt = false;
    pi.ifidx = WIFI_IF_STA;
    if (esp_now_add_peer(&pi) != ESP_OK) {
        LOG_E("TalkManager: add_peer失败");
        return false;
    }
    return true;
}

void TalkManager::sendBeaconNow() {
    TalkBeacon b;
    b.hdr.magic = TALK_MAGIC; b.hdr.ver = TALK_PROTO_VER;
    b.hdr.type = TALK_BEACON; b.hdr.seq = 0;
    strncpy(b.name, deviceName.c_str(), TALK_DEVNAME_MAX);
    b.name[TALK_DEVNAME_MAX] = '\0';
    esp_read_mac(b.peerMac, ESP_MAC_WIFI_STA);
    b.busy = (busyV || pttTalkingV || linkStateV == TALK_LINK_CONNECTED) ? 1 : 0;
    b.rsv = 0;
    uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    esp_now_send(bcast, (uint8_t*)&b, sizeof(b));
    lastBeaconMs = millis();
}

// ================= Talk任务 =================
void TalkManager::talkTaskFunc(void* param) {
    TalkManager* self = (TalkManager*)param;
    self->talkTaskLoop();
}

void TalkManager::talkTaskLoop() {
    RxItem it;
    while (true) {
        // 收包(50ms阻塞, 兼做心跳节拍)
        while (xQueueReceive(rxQueue, &it, pdMS_TO_TICKS(50)) == pdTRUE) {
            onRecv(it.mac, it.data, it.len, it.rssi);
        }
        if (linkStateV == TALK_LINK_IDLE) continue;
        // ---- TX语音泵: 音频任务编码的ADPCM帧→组VOICE包发ESP-NOW ----
        // PTT 16k: 24ms/帧≈42pps; 全双工8k: 48ms/帧≈21pps; 50ms节拍内泵空队列。
        if (linkStateV == TALK_LINK_CONNECTED && hasConnPeer) {
            uint8_t frame[TALK_VOICE_FRAME_BYTES];
            uint8_t codec = 0;
            bool last = false;
            uint8_t bcast[6] = {0};
            memcpy(bcast, connPeer.mac, 6);
            int pump = 0;
            while (pump < TXQ_LEN && pullTxFrame(frame, codec, last)) {
                TalkVoice v;
                v.hdr.magic = TALK_MAGIC; v.hdr.ver = TALK_PROTO_VER;
                v.hdr.type = TALK_VOICE; v.hdr.seq = voiceSeqV++;
                v.codec = codec;
                v.flags = last ? 0x01 : 0x00;
                v.pcmSeq = voicePcmSeqV;
                voicePcmSeqV += TALK_VOICE_SAMPLES;
                memcpy(v.data, frame, TALK_VOICE_FRAME_BYTES);
                if (esp_now_send(bcast, (uint8_t*)&v, sizeof(v)) != ESP_OK) {
                    break;   // 发送队列满则停泵(下节拍继续, 旧帧由TX环形自然淘汰)
                }
                notifyVoiceSent();
                pump++;
            }
        }
        unsigned long now = millis();
        // ---- 文字ACK超时重传(600ms无ACK重发整条, 最多3次; link断开即弃) ----
        {
            bool needResend = false;
            uint8_t rMsg = 0, rTotal = 0;
            String rText;
            xSemaphoreTake(mutex, portMAX_DELAY);
            if (pendRetries > 0 && linkStateV == TALK_LINK_CONNECTED && hasConnPeer &&
                (long)(now - pendNextMs) >= 0) {
                pendRetries--;
                pendNextMs = now + TALK_TEXT_ACK_MS;
                if (pendRetries == 0) {
                    LOG_W("TalkManager: 文字msgId=%d无ACK(放弃)", (int)pendMsgId);
                    pendText = String();
                } else {
                    needResend = true;
                    rMsg = pendMsgId; rTotal = pendTotal; rText = pendText;
                }
            }
            if (linkStateV != TALK_LINK_CONNECTED && pendRetries > 0) {
                pendRetries = 0;   // 断开即弃(重连后由用户重发)
                pendText = String();
            }
            xSemaphoreGive(mutex);
            if (needResend) {
                LOG_W("TalkManager: 文字msgId=%d重发(剩%d次)",
                      (int)rMsg, (int)pendRetries);
                sendTextFrags(rMsg, rTotal, rText);
            }
        }
        // BEACON心跳: 扫描期500ms/平时2秒
        unsigned long period = scanningV ? 500 : 2000;
        if (now - lastBeaconMs > period) sendBeaconNow();
        // 扫描超时: 页面控制stop, 此处兜底清过期(5秒未见)
        if (scanningV) {
            xSemaphoreTake(mutex, portMAX_DELAY);
            for (int i = 0; i < PEER_MAX; i++) {
                if (peers[i].valid && now - peers[i].lastSeenMs > 5000) {
                    peers[i].valid = false;
                }
            }
            xSemaphoreGive(mutex);
        }
        // 配对超时(15秒无应答回READY)
        static unsigned long pairStartMs = 0;
        static TalkLinkState lastSt = TALK_LINK_IDLE;
        if (linkStateV != lastSt) { lastSt = linkStateV; pairStartMs = now; }
        if ((linkStateV == TALK_LINK_PAIRING || linkStateV == TALK_LINK_INCOMING) &&
            now - pairStartMs > 15000) {
            linkStateV = TALK_LINK_READY;
            LOG_W("TalkManager: 配对超时");
        }
        // ---- 连接存活看门狗(对端异常断电/超出距离, 无LEAVE兜底) ----
        // 通道A: 6秒无对端任何合法帧(3个2秒BEACON周期);
        // 通道B: 单播连续发送失败≥20次(讲话中对端关机约0.5~1秒快速检出)。
        if (linkStateV == TALK_LINK_CONNECTED && hasConnPeer) {
            bool rxTimeout = lastPeerRxMsV != 0 &&
                             (long)(now - lastPeerRxMsV) > TALK_LINK_TIMEOUT_MS;
            bool txFailed = txFailCountV >= TALK_TX_FAIL_MAX;
            if (rxTimeout || txFailed) {
                LOG_W("TalkManager: 掉线判定(rx静默%lums, 连发失败%u次)",
                      (unsigned long)(now - lastPeerRxMsV), (unsigned)txFailCountV);
                breakLink(TALK_DROP_LOST);
            }
        }
    }
}
