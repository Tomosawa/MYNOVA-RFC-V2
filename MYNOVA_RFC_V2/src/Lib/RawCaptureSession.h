/* SubGHz RAW录制：GPIO ISR → 独立worker → LittleFS；UI只读取快照。 */
#ifndef RAW_CAPTURE_SESSION_H
#define RAW_CAPTURE_SESSION_H

#include <Arduino.h>
#include <FS.h>
#include <freertos/semphr.h>
#include "RFSignalDecoder.h"
#include "RFSubGhzPresets.h"

#define RAW_TEMP_FILE        "/raw_tmp.sub"
// 文件持续追加，批次/ISR环形长度不是整段录制上限；只保留计数溢出保护。
#define RAW_MAX_SAMPLES      UINT32_MAX
#define RAW_FLUSH_SAMPLES    512
#define RAW_RSSI_HISTORY     100
#define RAW_RSSI_POLL_MS     50
#define RAW_RSSI_BAR_H       24
#define RAW_RSSI_FLOOR       (-110)
#define RAW_RSSI_CEIL        (-40)
#define RAW_THRESHOLD_OFF    (-127)
#define RAW_GATE_HOLD_MS     1000

struct RawCaptureConfig {
    uint32_t frequencyHz = 433920000;
    uint8_t presetId = RF_PRESET_AM650;
    int16_t rssiThreshold = -85;
};

struct RawCaptureSnapshot {
    uint32_t samples = 0;
    uint32_t elapsedMs = 0;
    uint32_t edges = 0;
    int16_t rssi = RAW_RSSI_FLOOR;
    uint8_t history[RAW_RSSI_HISTORY] = {0};
    uint8_t head = 0;
    bool wrapped = false;
    bool recording = false;
    bool gateOpen = false;
    bool full = false;
    bool overrun = false;
    bool writeError = false;
};

class RadioHelper;

class RawCaptureSession {
public:
    ~RawCaptureSession();
    bool start(RadioHelper& radio, const RawCaptureConfig& cfg);
    void stop();
    void erase();
    RawCaptureSnapshot snapshot() const;
    bool hasData() const { auto s = snapshot(); return s.samples > 0 && !s.writeError && !s.full && !s.overrun; }
    uint32_t sampleCount() const { return snapshot().samples; }
    const char* errorText() const { return startError; }
    static void sidecarPath(int slot1Based, char* out, size_t n);
    static bool ensureMounted();
    static uint8_t rssiToBar(int16_t dbm);

private:
    RawCaptureConfig cfg;
    RadioHelper* radio = nullptr;
    File file;
    SemaphoreHandle_t workerDone = nullptr;
    volatile bool stopRequested = false;
    bool fileOpen = false;
    bool recording = false;
    bool hitMax = false;
    bool writeError = false;
    bool overrunLatched = false;
    bool gateOpen = false;
    bool signalSeen = false;
    uint32_t startedAt = 0;
    uint32_t lastAboveAt = 0;
    uint32_t sampleWritten = 0;
    uint16_t pending = 0;
    int32_t lineBuf[RAW_FLUSH_SAMPLES];
    char textBuf[RAW_FLUSH_SAMPLES * 8 + 16];
    uint8_t rssiHist[RAW_RSSI_HISTORY] = {0};
    uint8_t rssiIdx = 0;
    bool rssiWrap = false;
    uint32_t lastRssiPoll = 0;
    int16_t lastRssi = RAW_RSSI_FLOOR;
    int16_t rssiPeak = RAW_THRESHOLD_OFF;
    const char* startError = "";
    mutable portMUX_TYPE snapshotMux = portMUX_INITIALIZER_UNLOCKED;
    RawCaptureSnapshot published;

    bool openAndWriteHeader();
    void flushLine(bool force);
    void poll(int16_t rssiDbm);
    void pushRssi(int16_t dbm);
    void publish();
    static void worker(void* context);
};
#endif
