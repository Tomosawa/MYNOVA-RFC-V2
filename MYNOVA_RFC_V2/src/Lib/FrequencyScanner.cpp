/*
  单频持续接收。扫频先短驻留探测，发现载波或协议进展后留在当前频点。
*/

#include "FrequencyScanner.h"
#include <string.h>
#include "../Util/Log.h" // 分级日志

#define LOG_TAG "SCAN"

namespace {
constexpr unsigned long WeakProbeDwellMs = 160;
constexpr unsigned long QuietReleaseMs = 110;  // 跨过实测 56ms 间隔和既有长 Guard
constexpr unsigned long ScanLockWithoutFrameMs = 240;
constexpr unsigned long ScanLockLimitMs = 600;
constexpr int8_t WeakRssiThreshold = RSSI_THRESHOLD - 6;
constexpr uint8_t WeakRssiSamples = 2;
}

FrequencyScanner::FrequencyScanner(CC1101Driver& driver, RFSignalDecoder& decoder)
    : _cc1101(driver), _decoder(decoder), _frequencies(nullptr), _freqCount(0), _freqInfo(nullptr),
      _currentFreqIdx(0), _lastSwitchTime(0), _running(false), _scanMode(MODE_FAST_SCAN), _receiveFreqIdx(-1),
      _lastFrameTime(0), _signalEndTime(0), _receiveRSSI(-128),
      _frameCount(0), _hasLastFrame(false), _debug(false) {
    memset(&stats, 0, sizeof(stats));
    memset(&_lastFrame, 0, sizeof(_lastFrame));
    memset(_frameBuffer, 0, sizeof(_frameBuffer));
}

bool FrequencyScanner::begin(const float* frequencies, int count) {
    _running = false;
    if (!frequencies || count <= 0) return false;
    _frameCount = 0;
    _hasLastFrame = false;
    _receiveFreqIdx = -1;
    memset(_frameBuffer, 0, sizeof(_frameBuffer));
    // 释放旧的频率表（防止重复调用 begin 时内存泄漏）
    if (_frequencies) { delete[] _frequencies; _frequencies = nullptr; }
    if (_freqInfo)    { delete[] _freqInfo;    _freqInfo    = nullptr; }

    _freqCount   = count;
    _frequencies = new float[count];
    _freqInfo    = new FrequencyInfo[count];

    for (int i = 0; i < count; i++) {
        _frequencies[i]          = frequencies[i];
        _freqInfo[i].freqMHz     = frequencies[i];
        _freqInfo[i].rssi        = -128;
        _freqInfo[i].lastVisit   = 0;
        _freqInfo[i].hasSignal   = false;
    }

    if (!switchToFrequency(0)) return false;
    _running = true;
    // 单频持续解码，绝不按 fast dwell 重新调谐，也不因 RSSI 门限阻断。
    _scanMode = count == 1 ? MODE_RECEIVE : MODE_FAST_SCAN;
    if (count == 1) {
        _receiveFreqIdx = 0;
        _receiveRSSI = getCurrentRSSI();
        _lastFrameTime = millis();
        _signalEndTime = 0;
    }

    if (_debug) {
        LOG_I("[Scanner] Init: %d freqs, idle=%dms weak=%lums threshold=%ddBm",
              count, FAST_SCAN_DWELL_MS, WeakProbeDwellMs, RSSI_THRESHOLD);
    }
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// 读取 RSSI（多次均值，减少抖动）
// ──────────────────────────────────────────────────────────────────────────────
int8_t FrequencyScanner::getCurrentRSSI() {
    int32_t sum = 0;
    const int SAMPLES = 3;
    for (int i = 0; i < SAMPLES; i++) {
        uint8_t raw = _cc1101.readRSSI();
        int8_t rssi;
        if (raw >= 128) rssi = (int8_t)((raw - 256) / 2 - 74);
        else            rssi = (int8_t)((raw / 2)   - 74);
        sum += rssi;
        if (i < SAMPLES - 1) delayMicroseconds(200);
    }
    return (int8_t)(sum / SAMPLES);
}

// ──────────────────────────────────────────────────────────────────────────────
// 主循环
// ──────────────────────────────────────────────────────────────────────────────
void FrequencyScanner::loop() {
    if (!_running) return;
    if (_scanMode == MODE_FAST_SCAN) {
        fastScanMode();
    } else {
        receiveMode();
    }
    cleanupOldFrames();
}

// ──────────────────────────────────────────────────────────────────────────────
// 停止扫描：CC1101 进 IDLE，清空缓冲（退出接收页面时调用）
// ──────────────────────────────────────────────────────────────────────────────
void FrequencyScanner::stop() {
    _running = false;
    _cc1101.setIdleMode();
    _scanMode      = MODE_FAST_SCAN;
    _receiveFreqIdx = -1;
    _frameCount    = 0;
    _hasLastFrame  = false;
    _decoder.stopOokCapture();  // 释放 GPIO39，下一次 RAW 或发射可使用。
    _decoder.reset();
    if (_debug) LOG_I("[Scanner] Stopped");
}

// ──────────────────────────────────────────────────────────────────────────────
// 空闲时快速换频。弱 RSSI 延长观察，协议进展可在没有 RSSI 门限触发时锁频。
void FrequencyScanner::fastScanMode() {
    const int8_t rssi = getCurrentRSSI();
    _freqInfo[_currentFreqIdx].rssi = rssi;
    _freqInfo[_currentFreqIdx].lastVisit = millis();

    if (rssi > RSSI_THRESHOLD) {
        enterReceive(_currentFreqIdx, rssi);
        receiveMode();
        return;
    }
    _receiveRSSI = rssi;
    if (decodeAndProcessFrame()) return;

    const unsigned long now = millis();
    const unsigned long progressAt = _decoder.lastFrameProgressAt();
    if (_decoder.lastFrameCandidateAt() || (progressAt && now - progressAt < QuietReleaseMs)) {
        enterReceive(_currentFreqIdx, rssi);
        return;
    }

    if (rssi >= WeakRssiThreshold && _weakRssiSamples < WeakRssiSamples) ++_weakRssiSamples;
    const unsigned long dwell = _weakRssiSamples >= WeakRssiSamples ? WeakProbeDwellMs : FAST_SCAN_DWELL_MS;
    if (now - _lastSwitchTime < dwell) return;
    stats.totalScans++;
    if (!switchToFrequency((_currentFreqIdx + 1) % _freqCount)) stop();
}

void FrequencyScanner::enterReceive(int index, int8_t rssi) {
    _scanMode = MODE_RECEIVE;
    _receiveFreqIdx = index;
    _receiveRSSI = rssi;
    _scanLockAt = _lastFrameTime = millis();
    _quietSince = 0;
    _signalEndTime = 0;
    _frameCount = 0;
    _hasLastFrame = false;
    stats.signalDetected++;
    if (_debug) LOG_D("[Scanner] RX %.2fMHz RSSI=%ddBm", _frequencies[index], rssi);
}

// ──────────────────────────────────────────────────────────────────────────────
// 接收模式：持续读取信号直到信号消失或超时
// ──────────────────────────────────────────────────────────────────────────────
void FrequencyScanner::receiveMode() {
    // 单频模式在每轮更新 RSSI，解码不受锁频阈值限制。
    if (_freqCount == 1) _receiveRSSI = getCurrentRSSI();
    const bool newFrame = decodeAndProcessFrame();
    const unsigned long now = millis();
    // 已看到完整但尚待第二帧确认的波形时保留锁频；长Guard不能在中途触发跳频。
    unsigned long candidateAt = _decoder.lastFrameCandidateAt();
    if ((int32_t)(candidateAt - _lastFrameTime) > 0) _lastFrameTime = candidateAt;
    if (newFrame) {
        _lastFrameTime = now;
        _signalEndTime = 0;
        return;  // 先交付该频点的确认结果，避免先跳频。
    }

    // 只要缓冲区有帧，就开始监测信号是否结束
    if (_frameCount > 0) {
        if (_signalEndTime == 0) _signalEndTime = now;

        int8_t  curRSSI    = getCurrentRSSI();
        bool    signalGone = (curRSSI < RSSI_THRESHOLD - 5);

        // 估算单帧时长：24位 × (短+长)/2；最短 80ms，最长 400ms
        unsigned long minRxMs = 80;
        if (_frameBuffer[0].shortPulse > 0 && _frameBuffer[0].longPulse > 0) {
            minRxMs = (unsigned long)24 *
                      (_frameBuffer[0].shortPulse + _frameBuffer[0].longPulse) / 1000 + 50;
            if (minRxMs < 80)  minRxMs = 80;
            if (minRxMs > 400) minRxMs = 400;
        }
        bool timeout = (now - _signalEndTime > minRxMs);

        // 绝对超时保护：避免卡在接收模式
        bool absTimeout = (now - _lastFrameTime > FRAME_TIMEOUT_MS);

        if (signalGone || timeout || absTimeout) {
            if (_debug) {
                LOG_D("[Scanner] Confirm: %.2fMHz gone=%d t=%lums abs=%d frames=%d",
                      _frequencies[_receiveFreqIdx],
                      signalGone, now - _signalEndTime, absTimeout, _frameCount);
            }
            processPendingFrames();
            exitReceiveMode();
        }
    } else if (_freqCount == 1) {
        if (now - _lastFrameTime > FRAME_TIMEOUT_MS) exitReceiveMode();
    } else {
        const unsigned long progressAt = _decoder.lastFrameProgressAt();
        const bool progressing = progressAt && now - progressAt < QuietReleaseMs;
        if (_cc1101.getRSSIdBm() < WeakRssiThreshold) {
            if (!_quietSince) _quietSince = now;
        } else {
            _quietSince = 0;
        }

        const bool quiet = !candidateAt && !progressing && _quietSince &&
                           now - _quietSince >= QuietReleaseMs;
        const unsigned long lockAge = now - _scanLockAt;
        const bool expired = lockAge >= ScanLockLimitMs ||
            (candidateAt ? now - _lastFrameTime >= FRAME_TIMEOUT_MS
                         : lockAge >= (progressing ? FRAME_TIMEOUT_MS : ScanLockWithoutFrameMs));
        if (quiet || expired) {
            if (_debug) LOG_D("[Scanner] Resume scan: %s at %lums", quiet ? "quiet" : "timeout", lockAge);
            exitReceiveMode();
        }
    }
}

// 退出接收模式，回到快速扫描
void FrequencyScanner::exitReceiveMode() {
    if (_freqCount == 1) {
        // 清帧缓冲并继续监听；保留解码协议机的跨帧状态和 RX 连续性。
        _receiveRSSI = getCurrentRSSI();
        _lastFrameTime = millis();
        _signalEndTime = 0;
        return;
    }
    _scanMode = MODE_FAST_SCAN;
    _receiveFreqIdx = -1;
    for (int i = 0; i < _freqCount; i++) _freqInfo[i].rssi = -128;
    if (!switchToFrequency((_currentFreqIdx + 1) % _freqCount)) stop();
}

// ──────────────────────────────────────────────────────────────────────────────
// 从解码器读取一帧，加入缓冲区
// ──────────────────────────────────────────────────────────────────────────────
bool FrequencyScanner::decodeAndProcessFrame() {
    RFSignal signal;
    if (!_decoder.available(signal)) return false;

    // available() 的返回不等于协议已验证：最终上报门独立检查来源。
    // 只接受真实帧锚点和协议规则通过、至少两帧完全一致的结果。
    if (!signal.protocolValidated || signal.repeatCount < SIGNAL_CONFIRM_COUNT ||
        signal.bitCount < 8 || signal.bitCount > MAX_BITS || signal.syncPulse == 0)
        return false;

    SignalFrame frame;
    memset(&frame, 0, sizeof(frame));

    uint16_t bytes = (signal.bitCount + 7) / 8;
    if (bytes > 32) bytes = 32;
    memcpy(frame.data, signal.data, bytes);

    frame.bitCount   = signal.bitCount;
    frame.pulseCount = signal.pulseCount;
    frame.shortPulse = signal.shortPulse;
    frame.longPulse  = signal.longPulse;
    frame.syncPulse  = signal.syncPulse;
    frame.guardDurationUs = signal.guardDurationUs;
    frame.freqIndex  = _currentFreqIdx;
    frame.rssi       = _receiveRSSI;
    frame.timestamp  = millis();
    frame.repeatCount = signal.repeatCount;
    frame.protocolId = signal.protocolId;
    frame.protocolValidated = signal.protocolValidated;
    strncpy(frame.protocolName, signal.protocolName, sizeof(frame.protocolName) - 1);
    frame.confirmed = false;

    // 重复校验已在协议机内完成，不能把一次“已双帧确认”重新标成 Repeats: 1。
    _frameBuffer[0] = frame;
    _frameCount = 1;
    confirmFrame(_frameBuffer[0]);
    return true;
}

void FrequencyScanner::processPendingFrames() {
    // RSSI 下降或超时只能结束守候，不能把未验证候选提升成已确认信号。
    for (int i = _frameCount - 1; i >= 0; i--) {
        if (_frameBuffer[i].confirmed) continue;
        for (int j = i; j < _frameCount - 1; j++) _frameBuffer[j] = _frameBuffer[j + 1];
        _frameCount--;
    }
}

bool FrequencyScanner::confirmFrame(SignalFrame& frame) {
    frame.confirmed = true;
    stats.framesConfirmed++;
    printFrame(frame);
    return true;
}

void FrequencyScanner::printFrame(const SignalFrame& frame) {
#if LOG_COMPILE_MAX >= 4
    if (Log::getLevel() < LogLevel::DEBUG) return;
#else
    return;
#endif
    float freqMHz = (frame.freqIndex >= 0 && frame.freqIndex < _freqCount)
                    ? _frequencies[frame.freqIndex] : 0.0f;

    uint16_t bytes = (frame.bitCount + 7) / 8;
    char hex[65] = {0};
    for (uint16_t i = 0; i < bytes; i++)
        snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02X", frame.data[i]);
    LOG_D("[RX.confirm] freq=%.2fMHz bits=%u HEX=%s repeats=%u S=%u L=%u Guard=%lu proto=%s",
          freqMHz, frame.bitCount, hex, frame.repeatCount, frame.shortPulse,
          frame.longPulse, (unsigned long)frame.guardDurationUs, frame.protocolName);

    char bin[36] = {0};
    uint16_t showBits = frame.bitCount < 32 ? frame.bitCount : 32;
    for (uint16_t i = 0; i < showBits; i++)
        bin[i] = (frame.data[i / 8] >> (7 - (i % 8))) & 1 ? '1' : '0';
    if (frame.bitCount > 32) strcat(bin, "...");
    LOG_D("[RX.confirm] BIN: %s", bin);
}

void FrequencyScanner::cleanupOldFrames() {
    if (_scanMode == MODE_RECEIVE) return;

    unsigned long now = millis();
    for (int i = _frameCount - 1; i >= 0; i--) {
        if (_frameBuffer[i].confirmed) continue;
        if (now - _frameBuffer[i].timestamp > 600) {
            for (int j = i; j < _frameCount - 1; j++)
                _frameBuffer[j] = _frameBuffer[j + 1];
            _frameCount--;
        }
    }
}

bool FrequencyScanner::switchToFrequency(int index) {
    if (index < 0 || index >= _freqCount) return false;
    // 单个频点不能锁定时跳过它，避免整个频段停止；单频则明确报告失败。
    for (int attempt = 0; attempt < _freqCount; attempt++) {
        _currentFreqIdx = (index + attempt) % _freqCount;
        if (_cc1101.setFrequencyMHz(_frequencies[_currentFreqIdx])) {
            // 调谐已经进入 IDLE，只在新频点开始 RX 前清理协议状态。
            _decoder.reset();
            if (_cc1101.setRxMode()) {
                _decoder.restartOokCapture();
                _weakRssiSamples = 0;
                _lastSwitchTime = millis();
                return true;
            }
        }
        LOG_W("[Scanner] cannot enter RX at %.2fMHz", _frequencies[_currentFreqIdx]);
    }
    return false;
}

bool FrequencyScanner::hasConfirmedSignal() {
    for (int i = 0; i < _frameCount; i++)
        if (_frameBuffer[i].confirmed) return true;
    return false;
}

bool FrequencyScanner::getConfirmedSignal(SignalFrame& frame) {
    for (int i = 0; i < _frameCount; i++) {
        if (_frameBuffer[i].confirmed) {
            frame = _frameBuffer[i];
            _frameBuffer[i].confirmed = false;
            return true;
        }
    }
    return false;
}
