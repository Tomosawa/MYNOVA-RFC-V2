/*
  RawCaptureSession.cpp — SubGHz RAW 连续录制会话实现
*/

#include "RawCaptureSession.h"
#include <LittleFS.h>
#include "../Util/Log.h"
#include "../Util/TaskConfig.h"
#include "../Drivers/RadioHelper.h"

#define LOG_TAG "RAWSES"

RawCaptureSession::~RawCaptureSession() {
    stop();
}

bool RawCaptureSession::ensureMounted() {
    // 已挂载时 begin 幂等返回 true；不自动格式化，避免异常情况下清空分区
    if (LittleFS.begin(false)) return true;
    LOG_E("LittleFS mount failed");
    return false;
}

void RawCaptureSession::sidecarPath(int slot1Based, char* out, size_t n) {
    snprintf(out, n, "/rfraw_%d.sub", slot1Based);
}

uint8_t RawCaptureSession::rssiToBar(int16_t dbm) {
    int32_t v = dbm;
    if (v < RAW_RSSI_FLOOR) v = RAW_RSSI_FLOOR;
    if (v > RAW_RSSI_CEIL) v = RAW_RSSI_CEIL;
    return (uint8_t)((v - RAW_RSSI_FLOOR) * RAW_RSSI_BAR_H /
                     (RAW_RSSI_CEIL - RAW_RSSI_FLOOR));
}

void RawCaptureSession::pushRssi(int16_t dbm) {
    rssiHist[rssiIdx] = rssiToBar(dbm);
    rssiIdx++;
    if (rssiIdx >= RAW_RSSI_HISTORY) {
        rssiIdx = 0;
        rssiWrap = true;
    }
}

bool RawCaptureSession::openAndWriteHeader() {
    if (!ensureMounted()) return false;
    if (LittleFS.exists(RAW_TEMP_FILE) && !LittleFS.remove(RAW_TEMP_FILE)) return false;
    file = LittleFS.open(RAW_TEMP_FILE, FILE_WRITE);
    if (!file) return false;
    fileOpen = true;
    int n = snprintf(textBuf, sizeof(textBuf),
        "Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: %lu\nPreset: %s\nProtocol: RAW\n",
        (unsigned long)cfg.frequencyHz, RfPreset(cfg.presetId).flipperName);
    if (n <= 0 || (size_t)n >= sizeof(textBuf) ||
        file.write((const uint8_t*)textBuf, n) != (size_t)n) {
        file.close();
        fileOpen = false;
        return false;
    }
    file.flush();
    return true;
}

bool RawCaptureSession::start(RadioHelper& receiver, const RawCaptureConfig& newCfg) {
    stop();
    cfg = newCfg;
    startError = "";
    hitMax = writeError = overrunLatched = gateOpen = signalSeen = false;
    recording = false;
    stopRequested = false;
    sampleWritten = pending = rssiIdx = 0;
    rssiWrap = false;
    lastRssi = RAW_RSSI_FLOOR;
    rssiPeak = RAW_THRESHOLD_OFF;
    memset(rssiHist, 0, sizeof(rssiHist));
    portENTER_CRITICAL(&snapshotMux);
    published = RawCaptureSnapshot{};
    portEXIT_CRITICAL(&snapshotMux);

    // 文件头先落盘，再启动射频，避免建文件期间灌满ISR缓冲。
    if (!openAndWriteHeader()) {
        startError = "存储异常";
        return false;
    }
    if (!receiver.EnableRawReceive((float)cfg.frequencyHz / 1000000.0f, cfg.presetId)) {
        startError = "射频异常";
        file.close();
        fileOpen = false;
        return false;
    }
    radio = &receiver;
    workerDone = xSemaphoreCreateBinary();
    if (!workerDone) {
        startError = "内存不足";
        stop();
        return false;
    }
    radio->GetDecoder().rawReset();
    startedAt = lastRssiPoll = millis();
    recording = true;
    poll(radio->GetRawRssi());
    publish();
    if (xTaskCreatePinnedToCore(worker, TASK_RAW_NAME, TASK_RAW_STACK, this,
            TASK_PRIO_HW, nullptr, TASK_CORE_APP) != pdPASS) {
        // 未创建任务，不能等workerDone。
        vSemaphoreDelete(workerDone);
        workerDone = nullptr;
        startError = "内存不足";
        stop();
        return false;
    }
    LOG_I("RAW start: %luHz preset=%u threshold=%d RSSI=%d",
          (unsigned long)cfg.frequencyHz, cfg.presetId, cfg.rssiThreshold, lastRssi);
    return true;
}

void RawCaptureSession::flushLine(bool force) {
    if (!fileOpen || pending == 0 || writeError) return;
    if (!force && pending < RAW_FLUSH_SAMPLES) return;
    size_t len = snprintf(textBuf, sizeof(textBuf), "RAW_Data:");
    for (uint16_t i = 0; i < pending; i++) {
        len += snprintf(textBuf + len, sizeof(textBuf) - len, " %ld", (long)lineBuf[i]);
    }
    textBuf[len++] = '\n';
    if (file.write((const uint8_t*)textBuf, len) != len) {
        writeError = true;
        LOG_E("RAW file write failed at %lu samples", (unsigned long)sampleWritten);
    }
    // 每512段一次批量写入，停止时flush；避免每个数字写闪存/停中断。
    pending = 0;
}

void RawCaptureSession::poll(int16_t rssiDbm) {
    uint32_t now = millis();
    lastRssi = rssiDbm;
    if (rssiDbm > rssiPeak) rssiPeak = rssiDbm;
    if (now - lastRssiPoll >= RAW_RSSI_POLL_MS) {
        lastRssiPoll = now;
        // 显示50ms窗口的峰值，短遥控突发也能在图上留下痕迹。
        pushRssi(rssiPeak);
        rssiPeak = RAW_THRESHOLD_OFF;
    }
    if (cfg.rssiThreshold != RAW_THRESHOLD_OFF && rssiDbm >= cfg.rssiThreshold) {
        signalSeen = true;
        lastAboveAt = now;
    }
    gateOpen = cfg.rssiThreshold == RAW_THRESHOLD_OFF ||
               (signalSeen && now - lastAboveAt < RAW_GATE_HOLD_MS);
    RFSignalDecoder& decoder = radio->GetDecoder();
    decoder.rawSetGate(gateOpen);
    if (decoder.rawTakeOverrun()) overrunLatched = true;

    // 门限已在ISR入环前应用。信号结束后RSSI下降也不能丢弃已捕获的段。
    // 一轮最多排空整个环，防止噪声持续到来时死循环。
    RawSeg seg[128];
    for (uint16_t drained = 0; drained < RAW_RING_SIZE && !hitMax && !writeError;) {
        uint16_t n = decoder.rawDrain(seg, 128);
        if (!n) break;
        drained += n;
        for (uint16_t i = 0; i < n; i++) {
            int32_t v = seg[i].durUs;
            lineBuf[pending++] = seg[i].level ? v : -v;
            sampleWritten++;
            if (pending == RAW_FLUSH_SAMPLES) flushLine(true);
            if (sampleWritten == RAW_MAX_SAMPLES) hitMax = true;
            if (hitMax || writeError) break;
        }
    }
}

void RawCaptureSession::publish() {
    RawCaptureSnapshot s;
    s.samples = sampleWritten;
    s.elapsedMs = millis() - startedAt;
    s.edges = radio ? radio->GetDecoder().rawEdgeCount() : 0;
    s.rssi = lastRssi;
    memcpy(s.history, rssiHist, sizeof(rssiHist));
    s.head = rssiIdx;
    s.wrapped = rssiWrap;
    s.recording = recording;
    s.gateOpen = gateOpen;
    s.full = hitMax;
    s.overrun = overrunLatched;
    s.writeError = writeError;
    portENTER_CRITICAL(&snapshotMux);
    published = s;
    portEXIT_CRITICAL(&snapshotMux);
}

RawCaptureSnapshot RawCaptureSession::snapshot() const {
    portENTER_CRITICAL(&snapshotMux);
    RawCaptureSnapshot s = published;
    portEXIT_CRITICAL(&snapshotMux);
    return s;
}

void RawCaptureSession::worker(void* context) {
    auto* self = static_cast<RawCaptureSession*>(context);
    while (!self->stopRequested && !self->hitMax && !self->writeError) {
        self->poll(self->radio->GetRawRssi());
        self->publish();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    self->radio->QuiesceRawReceive();
    self->poll(self->lastRssi);  // 停止产生新边沿后，排空所有尾段
    self->flushLine(true);
    self->file.flush();
    self->file.close();
    self->fileOpen = false;
    self->recording = false;
    self->publish();
    LOG_I("RAW stopped: samples=%lu edges=%lu RSSI=%d%s%s",
          (unsigned long)self->sampleWritten,
          (unsigned long)self->radio->GetDecoder().rawEdgeCount(), self->lastRssi,
          self->overrunLatched ? " OVERRUN" : "", self->writeError ? " WRITE_ERROR" : "");
    // 发信号后不再访问self，析构方可以安全释放页面。
    SemaphoreHandle_t done = self->workerDone;
    xSemaphoreGive(done);
    vTaskDelete(nullptr);
}

void RawCaptureSession::stop() {
    if (workerDone) {
        stopRequested = true;
        xSemaphoreTake(workerDone, portMAX_DELAY);
        vSemaphoreDelete(workerDone);
        workerDone = nullptr;
    }
    if (fileOpen) {
        file.close();
        fileOpen = false;
    }
    recording = false;
    portENTER_CRITICAL(&snapshotMux);
    published.recording = false;
    portEXIT_CRITICAL(&snapshotMux);
    if (radio) {
        radio->DisableRawReceive();
        radio = nullptr;
    }
}

void RawCaptureSession::erase() {
    stop();
    if (ensureMounted() && LittleFS.exists(RAW_TEMP_FILE)) LittleFS.remove(RAW_TEMP_FILE);
    sampleWritten = pending = 0;
    portENTER_CRITICAL(&snapshotMux);
    published = RawCaptureSnapshot{};
    portEXIT_CRITICAL(&snapshotMux);
}
