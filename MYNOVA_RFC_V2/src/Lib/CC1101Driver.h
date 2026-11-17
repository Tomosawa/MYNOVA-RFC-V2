/*
  CC1101 Driver Class for ESP32
  
  提供便捷的 CC1101 配置和控制接口
*/

#ifndef CC1101DRIVER_H
#define CC1101DRIVER_H

#include <Arduino.h>
#include <SPI.h>
#include <atomic>
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>

// CC1101 寄存器定义
#define CC1101_IOCFG2       0x00
#define CC1101_IOCFG1       0x01
#define CC1101_IOCFG0       0x02
#define CC1101_FIFOTHR      0x03
#define CC1101_SYNC1        0x04
#define CC1101_SYNC0        0x05
#define CC1101_PKTLEN       0x06
#define CC1101_PKTCTRL1     0x07
#define CC1101_PKTCTRL0     0x08
#define CC1101_ADDR         0x09
#define CC1101_CHANNR       0x0A
#define CC1101_FSCTRL1      0x0B
#define CC1101_FSCTRL0      0x0C
#define CC1101_FREQ2        0x0D
#define CC1101_FREQ1        0x0E
#define CC1101_FREQ0        0x0F
#define CC1101_MDMCFG4      0x10
#define CC1101_MDMCFG3      0x11
#define CC1101_MDMCFG2      0x12
#define CC1101_MDMCFG1      0x13
#define CC1101_MDMCFG0      0x14
#define CC1101_DEVIATN      0x15
#define CC1101_MCSM2        0x16
#define CC1101_MCSM1        0x17
#define CC1101_MCSM0        0x18
#define CC1101_FOCCFG       0x19
#define CC1101_BSCFG        0x1A
#define CC1101_AGCCTRL2     0x1B
#define CC1101_AGCCTRL1     0x1C
#define CC1101_AGCCTRL0     0x1D
#define CC1101_WOREVT1      0x1E
#define CC1101_WOREVT0      0x1F
#define CC1101_WORCTRL      0x20
#define CC1101_FREND1       0x21
#define CC1101_FREND0       0x22
#define CC1101_FSCAL3       0x23
#define CC1101_FSCAL2       0x24
#define CC1101_FSCAL1       0x25
#define CC1101_FSCAL0       0x26
#define CC1101_RCCTRL1      0x27
#define CC1101_RCCTRL0      0x28
#define CC1101_FSTEST       0x29
#define CC1101_PTEST        0x2A
#define CC1101_AGCTEST      0x2B
#define CC1101_TEST2        0x2C
#define CC1101_TEST1        0x2D
#define CC1101_TEST0        0x2E

// CC1101 命令
#define CC1101_SRES         0x30
#define CC1101_SFSTXON      0x31
#define CC1101_SXOFF        0x32
#define CC1101_SCAL         0x33
#define CC1101_SRX          0x34
#define CC1101_STX          0x35
#define CC1101_SIDLE        0x36
#define CC1101_SAFC         0x37
#define CC1101_SWOR         0x38
#define CC1101_SPWD         0x39
#define CC1101_SFRX         0x3A
#define CC1101_SFTX         0x3B
#define CC1101_SWORRST      0x3C
#define CC1101_SNOP         0x3D

// 状态寄存器
#define CC1101_PARTNUM      0xF0
#define CC1101_VERSION      0xF1
#define CC1101_FREQEST      0xF2
#define CC1101_LQI          0xF3
#define CC1101_RSSI         0xF4
#define CC1101_MARCSTATE    0xF5
#define CC1101_WORTIME1     0xF6
#define CC1101_WORTIME0     0xF7
#define CC1101_PKTSTATUS    0xF8
#define CC1101_VCO_VC_DAC   0xF9
#define CC1101_TXBYTES      0xFA
#define CC1101_RXBYTES      0xFB

// 其他常量
#define CC1101_PATABLE      0x3E
#define CC1101_TXFIFO       0x3F
#define CC1101_RXFIFO       0x3F
#define CC1101_READ_SINGLE  0x80
#define CC1101_READ_BURST   0xC0
#define CC1101_WRITE_BURST  0x40

// 频率常量 (单位: Hz)
#define CC1101_FREQ_315MHZ  315000000
#define CC1101_FREQ_433MHZ  433920000
#define CC1101_FREQ_868MHZ  868000000
#define CC1101_FREQ_915MHZ  915000000

// 数据率常量
#define CC1101_DATARATE_1200    1200
#define CC1101_DATARATE_2400    2400
#define CC1101_DATARATE_4800    4800
#define CC1101_DATARATE_9600    9600
#define CC1101_DATARATE_19200   19200
#define CC1101_DATARATE_38400   38400

// 调制方式
enum CC1101_Modulation {
    MOD_2FSK = 0,
    MOD_GFSK = 1,
    MOD_ASK_OOK = 3,
    MOD_4FSK = 4,
    MOD_MSK = 7
};

// 工作模式
enum CC1101_Mode {
    MODE_IDLE = 0,
    MODE_RX = 1,
    MODE_TX = 2
};

// 任意脉冲发射的调制类型（txPulses）
enum CC1101_TxMod {
    TXMOD_OOK = 0,       // ASK/OOK 异步（PATABLE index0=关载波）
    TXMOD_2FSK_2K38 = 1, // 2FSK 频偏 2.38kHz
    TXMOD_2FSK_47K6 = 2  // 2FSK 频偏 47.6kHz
};

class CC1101Driver {
public:
    // 构造函数
    CC1101Driver();
    
    // 初始化
    bool begin(int sck, int miso, int mosi, int cs, int gdo0, int gdo2);
    void end();
    
    // 基础操作
    bool reset();
    bool checkConnection();
    
    // 频率设置 (Hz)
    bool setFrequency(uint32_t freq_hz);
    bool setFrequencyMHz(float freq_mhz);
    uint32_t getFrequency();
    
    // 预设频率
    void setFrequency315MHz() { setFrequency(CC1101_FREQ_315MHZ); }
    void setFrequency433MHz() { setFrequency(CC1101_FREQ_433MHZ); }
    void setFrequency868MHz() { setFrequency(CC1101_FREQ_868MHZ); }
    void setFrequency915MHz() { setFrequency(CC1101_FREQ_915MHZ); }
    
    // 数据率设置
    bool setDataRate(uint32_t baud);
    uint32_t getDataRate();
    
    // 调制方式设置
    void setModulation(CC1101_Modulation mod);
    CC1101_Modulation getModulation();
    
    // 发射功率设置 (0-7, 7为最大, OOK 在 PATABLE[1])
    // 433MHz OOK 实测表 (TI DN013): -30,-20,-15,-10,-5,0,+5,+10 dBm
    void setTxPower(int level);
    static uint8_t txPowerToPaByte(int level);
    static int txPowerToDbm(int level);
    
    // 通道设置
    void setChannel(uint8_t channel);
    uint8_t getChannel();
    
    // 同步字设置
    void setSyncWord(uint16_t sync);
    void disableSyncWord();
    
    // 工作模式
    bool setRxMode(); // MARCSTATE 确认 RX，包含校准等待
    bool setTxMode();
    void setIdleMode();
    
    // 数据包控制
    void enablePacketMode(uint8_t length);
    void disablePacketMode();
    void setAddress(uint8_t addr);
    void disableAddressCheck();
    
    // GDO 配置
    void setGDO0Output(uint8_t config);
    void setGDO2Output(uint8_t config);
    
    // FIFO 操作
    void flushRx();
    void flushTx();
    uint8_t getRxBytes();
    uint8_t getTxBytes();
    void writeTxFifo(uint8_t *data, uint8_t length);
    
    // 数据收发 (包模式)
    bool sendPacket(uint8_t *data, uint8_t length);
    int receivePacket(uint8_t *buffer, uint8_t maxLength);
    
    // FIFO 模式发射原始波形 (用于精确时序控制)
    // dataRate: 采样率 (bps)，应为脉冲宽度的倒数 (如 300us 脉冲 -> 3333 bps)
    // data: 位数据，每个bit代表一个采样周期的电平 (1=高, 0=低)
    // bitCount: 总位数
    bool transmitRawFIFO(uint32_t dataRate, uint8_t *data, uint16_t bitCount);
    
    // 直接发射PT2262格式信号
    // data: 数据位
    // bitCount: 位数
    // shortPulse: 短脉冲宽度 (us)
    // longPulse: 长脉冲宽度 (us)
    // syncPulse: 同步脉冲宽度 (us)
    // repeatCount: 重复发射次数
    // txPowerLevel: 发射功率档 0-7 (7=最大+10dBm, 默认7)
    bool transmitPT2262(const uint8_t *data, uint16_t bitCount,
                        uint16_t shortPulse, uint16_t longPulse, uint16_t syncPulse,
                        uint8_t repeatCount = 1, int txPowerLevel = 7);

    // 发射任意 (level, duration) 段序列（通用脉冲发射，Flipper .sub / 各协议 encoder 适配）
    // durations[i]/levels[i]: 一帧的完整段序列（level=true 即 GDO0 高 = 载波开）
    // count: 段数；repeatCount: 帧重复次数（帧间不插 gap——段序列自带帧头长 low Guard）
    // 段时长 >32767us 自动拆分为多个同电原子段（RMT 单 duration 15bit 上限）
    // txMod: TXMOD_OOK / TXMOD_2FSK_2K38 / TXMOD_2FSK_47K6（FM 段序列需对应录制预设）
    bool transmitPulses(const uint16_t* durations, const bool* levels, uint16_t count,
                        uint8_t repeatCount = 1, int txPowerLevel = 7, uint8_t txMod = TXMOD_OOK);

    // RAW符号和32位时长直接展开，不截短长间隔；使用录制时同一预设。
    bool transmitRaw(const int32_t* pulses, uint16_t count, int txPowerLevel, uint8_t presetId);

    using RawReadCallback = int (*)(void*, int32_t&);
    bool transmitRawStream(RawReadCallback read, void* context, int txPowerLevel, uint8_t presetId);
    bool transmitRawGpioStream(RawReadCallback read, void* context, int txPowerLevel, uint8_t presetId);

    void cancelTransmit() { _cancelTransmit.store(true); }
    void resetTransmitCancel() { _cancelTransmit.store(false); }
    bool transmitCancelled() const { return _cancelTransmit.load(); }

    // 加载 (寄存器地址,值) 对序列（以 {0,0} 结束，Flipper cc1101_configs 格式）
    // includePaTable=true 时把结束标记后的 8 字节写入 PATABLE
    bool loadRegisterPairs(const uint8_t* pairs, bool includePaTable = false);

    // 状态读取
    uint8_t readStatus(uint8_t addr);
    uint8_t readRSSI();
    uint8_t readLQI();
    uint8_t getMARCState();
    int16_t getRSSIdBm();
        
    // 寄存器读写
    void writeReg(uint8_t addr, uint8_t value);
    uint8_t readReg(uint8_t addr);
    void writeBurst(uint8_t addr, uint8_t *data, uint8_t length);
    void readBurst(uint8_t addr, uint8_t *buffer, uint8_t length);
    void sendCommand(uint8_t cmd);
    
    // 中断相关
    void enableCarrierSense();
    void disableCarrierSense();
    bool carrierDetected();
    
    // 调试
    void printRegisters();
    
    // 恢复默认配置 (发射后调用)
    void restoreDefaultConfig();
    
private:
    std::atomic<bool> _cancelTransmit{false};
    rmt_channel_handle_t _rmtTxChannel = nullptr;
    rmt_encoder_handle_t _rmtEncoder = nullptr;
    bool transmitSequence(const uint16_t* durations, const bool* levels, const int32_t* raw,
                          uint16_t count, uint8_t repeatCount, int txPowerLevel,
                          uint8_t txMod, int presetId, bool continuousRepeats = false);
    bool prepareRawTransmit(int presetId, int txPowerLevel);
    bool initRmtTransmit();
    bool writeRmt(rmt_data_t* items, uint32_t count);
    void finishRmtTransmit();
    int _sck, _miso, _mosi, _cs, _gdo0, _gdo2;
    uint32_t _frequency;
    uint32_t _dataRate;
    CC1101_Modulation _modulation;
    bool _initialized;
    
    void spiInit();
    bool csLow(); // 等待 CHIP_RDYn；超时不发送 SPI 命令
    void csHigh();
    uint8_t spiTransfer(uint8_t data);
    bool waitMisoHigh(uint32_t timeout_ms);
    bool waitMisoLow(uint32_t timeout_ms);
    
    // 计算频率寄存器值
    void calculateFrequencyRegs(uint32_t freq, uint8_t &freq2, uint8_t &freq1, uint8_t &freq0);
    // 计算数据率寄存器值
    void calculateDataRateRegs(uint32_t baud, uint8_t &mdmcfg4, uint8_t &mdmcfg3);
};

#endif
