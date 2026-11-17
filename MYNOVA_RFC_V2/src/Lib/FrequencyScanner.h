/*
  FrequencyScanner.h - 多频率快速扫描器（改进版）
*/

#ifndef FREQUENCY_SCANNER_H
#define FREQUENCY_SCANNER_H

#include <Arduino.h>
#include "CC1101Driver.h"
#include "RFSignalDecoder.h"

// 扫描配置
#define FAST_SCAN_DWELL_MS    35     // 空闲频点的短驻留
#define RSSI_THRESHOLD        -90    // 仅扫描锁频使用；单频不依赖 RSSI 门限
#define SIGNAL_CONFIRM_COUNT  2      // 至少重复 N 次才确认
#define FRAME_TIMEOUT_MS      350    // 接收模式绝对超时（ms）

struct FrequencyInfo {
    float         freqMHz;
    int8_t        rssi;
    unsigned long lastVisit;
    bool          hasSignal;
};

struct SignalFrame {
    uint8_t       data[32];
    uint16_t      bitCount;
    uint16_t      pulseCount;
    uint16_t      shortPulse;
    uint16_t      longPulse;
    uint16_t      syncPulse;
    uint32_t      guardDurationUs;
    int           freqIndex;
    int8_t        rssi;
    unsigned long timestamp;
    uint8_t       repeatCount;
    char          protocolName[32];
    uint8_t       protocolId;
    bool          protocolValidated;
    bool          confirmed;
};

class FrequencyScanner {
public:
    FrequencyScanner(CC1101Driver& driver, RFSignalDecoder& decoder);

    bool begin(const float* frequencies, int count);
    void loop();
    void stop();   // 停止扫描（CC1101 进 IDLE，丢弃未确认帧）

    bool hasConfirmedSignal();
    bool getConfirmedSignal(SignalFrame& frame);
    int  getCurrentFreqIndex() { return _currentFreqIdx; }
    int8_t getCurrentRSSI();
    bool isRunning() const { return _running; }

    void setDebug(bool enable) { _debug = enable; }

    struct {
        unsigned long totalScans;
        unsigned long signalDetected;
        unsigned long framesConfirmed;
        unsigned long framesDropped;
    } stats;

private:
    CC1101Driver&   _cc1101;
    RFSignalDecoder& _decoder;

    float*          _frequencies;
    int             _freqCount;
    FrequencyInfo*  _freqInfo;

    int             _currentFreqIdx;
    unsigned long   _lastSwitchTime;
    uint8_t         _weakRssiSamples = 0;
    unsigned long   _scanLockAt = 0;
    unsigned long   _quietSince = 0;
    bool            _running;

    enum ScanMode { MODE_FAST_SCAN, MODE_RECEIVE } _scanMode;
    int             _receiveFreqIdx;
    unsigned long   _lastFrameTime;
    unsigned long   _signalEndTime;
    int8_t          _receiveRSSI;

    SignalFrame     _frameBuffer[5];
    int             _frameCount;
    SignalFrame     _lastFrame;
    bool            _hasLastFrame;

    bool            _debug;

    bool  switchToFrequency(int index);
    void  fastScanMode();
    void  receiveMode();
    void  exitReceiveMode();
    void  enterReceive(int index, int8_t rssi);
    bool  decodeAndProcessFrame();
    void  processPendingFrames();
    bool  confirmFrame(SignalFrame& frame);
    void  printFrame(const SignalFrame& frame);
    void  cleanupOldFrames();
};

#endif
