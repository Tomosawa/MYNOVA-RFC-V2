/*
  RF Signal Decoder - Improved Header
  接收：PT2262、EV1527、SMC5326、CAME、Nice FLO、Holtek，以及严格成帧的 PWM 兼容解码。
  通用 PWM/Manchester 等分析接口不用于现场接收确认。
*/

#ifndef RF_SIGNAL_DECODER_H
#define RF_SIGNAL_DECODER_H

#include <Arduino.h>
#include "RFProtocolDecoders.h"
#include "RFProtocolCameStyle.h"
#include "RFProtocolPWM24.h"
#ifdef ARDUINO_ARCH_ESP32
#include "OokRmtCapture.h"
#include "OokFrameDecoder.h"
#endif

#define MAX_BITS   256
#define MAX_PULSES 512

// ── RAW 连续录制模式 ──（对齐 Flipper subghz_worker：ISR 产段、消费侧排空）
#define RAW_RING_SIZE    4096   // 对齐Flipper worker深度；ISR生产，后台任务消费
#define RAW_SEG_MAX_US   60000  // 单段时长上限（us），超长拆成同电平连续段
#define RAW_MIN_US       30     // 与普通RX/Flipper worker一致，保留33/66us有效短段
#define RX_TRACE_SIZE   132    // 64个数据位对+同步HIGH+首尾Guard

// RAW 段（durUs=电平持续 us，level=段真实电平，true=载波高）
struct RawSeg {
    uint16_t durUs;
    uint8_t  level;
    uint8_t  durationHigh; // 普通RX时长的高8位；RAW段<=60000us，此字段为0
};

enum SignalType {
    SIGNAL_UNKNOWN = 0,
    SIGNAL_FIXED_CODE,
    SIGNAL_ROLLING_CODE,
    SIGNAL_LEARNING_CODE,
    SIGNAL_RAW
};

enum EncodingType {
    ENCODING_UNKNOWN = 0,
    ENCODING_PWM,
    ENCODING_PPM,
    ENCODING_MANCHESTER,
    ENCODING_BI_PHASE,
    ENCODING_RAW
};

struct RFSignal {
    float         frequency;
    unsigned long timestamp;
    uint16_t      pulseCount;
    uint16_t      bitCount;
    uint16_t      shortPulse;
    uint16_t      longPulse;
    uint16_t      syncPulse;
    uint32_t      guardDurationUs; // 完整间隔，syncPulse保留旧接口的16位表示
    uint16_t      packetGap;
    uint8_t       data[MAX_BITS / 8];
    char          hexString[MAX_BITS / 4 + 1];
    char          binaryString[MAX_BITS + 1];
    uint16_t      pulses[MAX_PULSES];
    bool          pulseLevels[MAX_PULSES];
    SignalType    signalType;
    EncodingType  encodingType;
    int8_t        rssi;
    uint8_t       repeatCount;
    char          protocolName[32];
    uint8_t       protocolId;
    bool          protocolValidated; // 真实帧锚点、协议规则及重复帧均通过
};

struct ProtocolProfile {
    const char*  name;
    uint8_t      id;
    uint16_t     shortMin, shortMax;
    uint16_t     longMin,  longMax;
    uint16_t     syncMin,  syncMax;
    uint8_t      bitCount;
    EncodingType encoding;
};

class RFSignalDecoder {
public:
    RFSignalDecoder();

    void begin(int gdo0Pin, int gdo2Pin);
    void end();
    void beginOokCapture(int pin);
    void restartOokCapture();
    void stopOokCapture();

    // ── RAW 连续录制模式（与普通成帧接收互斥，进录制会话时开启，退出关闭）──
    void enableRawMode(bool on, uint16_t minUs = RAW_MIN_US);
    void finishRawCapture();
    bool isRawMode() const { return _rawMode; }
    void rawReset();
    void rawSetGate(bool open);     // RSSI门限作用于新脉冲，已入环的数据仍保留
    // 排空环形缓冲（消费侧调用，拷贝到 out，返回段数）
    uint16_t rawDrain(RawSeg* out, uint16_t maxCount);
    bool rawTakeOverrun();          // 读取并清除溢出标志
    uint32_t rawProduced() const { return _rawProduced; }  // 入环总段数（reset 后）
    uint32_t rawEdgeCount() const { return _rawEdges; }
    uint32_t edgeCount() const { return _edgeCount; } // 所有模式，包含噪声和门限外边沿
    uint32_t receiveOverruns() const { return _rxOverruns; }
    // 滤波前GPIO边沿窗口，诊断短脉冲是否被滤掉；复制期间仍保留原窗口。
    uint16_t edgeSnapshot(RawSeg* out, uint16_t capacity, uint32_t& total, uint32_t& glitches);
    unsigned long lastFrameCandidateAt() const { return _lastFrameCandidateAt; }
    unsigned long lastFrameProgressAt() const { return _lastFrameProgressAt; }
    // DEBUG级诊断；默认INFO立即返回，不格式化或打印波形/协议统计。
    void logReceiveDiagnostics(bool strongSignal);

    static void IRAM_ATTR handleInterrupt();
    void        IRAM_ATTR processInterrupt();

    bool available();
    bool available(RFSignal &signal);

    RFSignal* getLastSignal();
    void      reset();

    // 解码函数（公开供测试）
    bool     decodePWM(const uint16_t* pulses, uint16_t count, RFSignal &signal);
    bool     decodePPM(const uint16_t* pulses, uint16_t count, RFSignal &signal);
    bool     decodeManchester(const uint16_t* pulses, const bool* levels,
                              uint16_t count, RFSignal &signal);
    bool     decodeBiPhase(const uint16_t* pulses, const bool* levels,
                           uint16_t count, RFSignal &signal);

    uint16_t decodePWMFrameRaw(const uint16_t* pulses, uint16_t start, uint16_t end,
                               uint16_t shortPulse, uint16_t longPulse, uint8_t* data,
                               uint16_t* errOut = nullptr);
    bool     decodePWMFrame(const uint16_t* pulses, uint16_t start, uint16_t end,
                            uint16_t midPoint, RFSignal &signal);

    bool     identifyProtocol(RFSignal &signal);

    // Flipper 式回放解码：把 ISR 记录的段序列回放进独立协议机
    // 返回 true 表示某协议连续两帧确认，23bit 截断帧返回 false（静默丢弃）
    bool     decodeFlipperStyle(const uint16_t* pulses, const bool* levels,
                                uint16_t count, RFSignal &signal);
    bool     decodeFlipperStyle(const uint32_t* pulses, const bool* levels,
                                uint16_t count, RFSignal &signal);

    void analyzeTiming(const uint16_t* pulses, uint16_t count,
                       uint16_t &shortPulse, uint16_t &longPulse, uint16_t &syncPulse);
    bool extractBits(const uint16_t* pulses, uint16_t count,
                     uint16_t shortPulse, uint16_t longPulse,
                     uint8_t* data, uint16_t maxBits);

    static void    pulsesToHex(const uint8_t* data, uint16_t bits, char* hexStr);
    static void    pulsesToBinary(const uint8_t* data, uint16_t bits, char* binStr);
    static uint8_t reverseBits(uint8_t byte);
    static uint32_t crc32(const uint8_t* data, uint16_t length);

    bool  isDuplicate(const RFSignal &sig1, const RFSignal &sig2);
    float calculateSimilarity(const RFSignal &sig1, const RFSignal &sig2);

    void printSignal(const RFSignal &signal);
    void printRawPulses(const RFSignal &signal);
    void printStatistics();

    static const char* getEncodingName(EncodingType enc);
    static const char* getSignalTypeName(SignalType type);

    void setMinPulseWidth(uint16_t us) { _minPulseWidth = us; }
    void setMaxPulseWidth(uint16_t us) { _maxPulseWidth = us; }
    void setPacketGap(uint16_t us)     { _packetGap = us; }
    void setNoiseFloor(int8_t rssi)    { _noiseFloor = rssi; }
    void setVerboseDebug(bool v)       { _verboseDebug = v; }

private:
    static RFSignalDecoder* _instance;
#ifdef ARDUINO_ARCH_ESP32
    OokRmtCapture _ookCapture;
    OokFrameDecoder _ookFrame;
    bool _ookRmtActive = false;
    bool readOokCapture();
#endif

    int  _gdo0Pin;
    int  _gdo2Pin;
    volatile unsigned long _lastInterruptTime;
    volatile unsigned long _lastPulseTime;
    bool     _rxStarted = false;
    bool     _rxPendingValid = false;
    bool     _rxPendingLevel = false;
    uint32_t _rxAccumUs = 0;
    volatile uint32_t _rxOverruns = 0;
    volatile uint32_t _rxGlitches = 0;
    RawSeg _edgeTrace[RX_TRACE_SIZE]{};
    uint16_t _edgeTraceHead = 0, _edgeTraceCount = 0;
    uint32_t _edgeTraceTotal = 0;
    uint32_t _lastEdgeDumpTotal = 0;
    uint8_t _edgeTraceDumps = 0;
    unsigned long _lastFrameCandidateAt = 0;
    unsigned long _lastFrameProgressAt = 0;
    uint32_t _rxSegments = 0, _rxLowGuards = 0, _rxHighGuards = 0;
    uint32_t _traceDur[RX_TRACE_SIZE]{};
    bool _traceLevel[RX_TRACE_SIZE]{};
    uint16_t _traceHead = 0, _traceCount = 0;
    uint8_t _traceDumps = 0;
    uint32_t _lastTraceDumpSegmentCount = 0;
    uint8_t _frameTraceDumps = 0;
    struct ProtocolProgress {
        uint32_t anchors = 0, ends = 0, badPairs = 0;
        uint8_t lastBits = 0;
        uint64_t lastData = 0;
    } _protocolProgress[RFProtocolRegistry::MAX_DECODERS][2];

    RFSignal _decodedSignal;
    RFSignal _lastSignal;

    uint16_t _minPulseWidth;
    uint16_t _maxPulseWidth;
    uint16_t _packetGap;
    int8_t   _noiseFloor;
    bool     _verboseDebug;

    uint32_t _totalSignals;
    uint32_t _decodedSignals;
    uint32_t _failedDecodes;

    static const ProtocolProfile PROTOCOLS[];
    static const uint8_t         PROTOCOL_COUNT;

    // Flipper 式协议机（成员变量保证 last_data 双帧确认跨 available() 调用连续）
    // 全部经 RFProtocolRegistry 统一注册/遍历，新增协议：
    //   1. 实现协议类（RFProtocolRegistry.h 接口）
    //   2. 在此加成员 + 构造函数 registerProtocols() 中注册
    PWMProtocolDecoder _protoPT2262;
    PWMProtocolDecoder _protoEV1527;
    PWMProtocolDecoder _protoSMC5326;
    CameStyleProtocolDecoder _protoCAME{&PROTO_CAME};
    CameStyleProtocolDecoder _protoNiceFlo{&PROTO_NICE_FLO};
    CameStyleProtocolDecoder _protoHoltek{&PROTO_HOLTEK};
    FramedPWMDecoder _protoPWM;
    bool _invert;   // GDO 电平极性：false=ISR取反后即真实电平；某次反极性命中后粘性置true
    RFDecState _alternateStates[RFProtocolRegistry::MAX_DECODERS]; // 另一极性的独立跨帧状态

    // ── RAW 连续录制（ISR 单生产者，rawDrain 单消费者）──
    volatile bool     _rawMode;
    volatile uint16_t _rawHead;     // ISR 写位置（下一个空槽）
    volatile uint16_t _rawTail;     // 消费位置
    volatile uint32_t _rawAccumUs;  // 不足滤除门限的待粘合时长
    portMUX_TYPE      _rawMux = portMUX_INITIALIZER_UNLOCKED;
    bool              _rawPendingLevel = false;
    bool              _rawPendingValid = false;
    bool              _rawStarted = false;
    bool              _rawGate = true;
    volatile uint32_t _rawEdges = 0;
    volatile uint32_t _edgeCount = 0;
    volatile bool     _rawOverrun;
    volatile uint32_t _rawProduced;
    uint16_t          _rawMinUs;
    RawSeg            _rawRing[RAW_RING_SIZE]; // RAW/普通接收互斥，共用连续段队列
    void flushRawTail(bool closingGate);  // 调用方持有 _rawMux
    void IRAM_ATTR rawPushIsr(bool level, uint32_t durUs);  // 入环（含同电平合并/拆分）

    void registerProtocols();  // 注册全部协议机到全局 RFProtocolRegistry（幂等）

    bool decodeSignal(const uint32_t* pulses, const bool* levels, uint16_t count);
    void logCompleteFrame(uint16_t remainingSegments, bool guardLevel, uint8_t bits);
    template <typename Pulse>
    bool decodeProtocolStream(const Pulse* pulses, const bool* levels,
                              uint16_t count, RFSignal& signal);
    void clearBuffer();
    bool isValidPulse(uint16_t width) { return width >= _minPulseWidth && width <= _maxPulseWidth; }
    uint16_t median(uint16_t* values, uint16_t count);
};

#endif
