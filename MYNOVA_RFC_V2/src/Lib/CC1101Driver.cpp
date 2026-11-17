/*
  CC1101 Driver Implementation
*/

#include "CC1101Driver.h"
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include "RFSubGhzPresets.h"
#include "RawRmtStream.h"
#include "RawGpioStream.h"
#include "RFTransmitConfig.h"
#include <new>
#include "../Util/Log.h" // 分级日志

#define LOG_TAG "CC1101"

// 发射功率表（433/315MHz OOK, TI DN013 实测值, 下标 0..7 由弱到强）
// 对应 dBm: -30, -20, -15, -10, -5, 0, +5, +10
static const uint8_t PA_TABLE_OOK_433[8] = {0x12, 0x0D, 0x1C, 0x34, 0x51, 0x85, 0xCB, 0xC2};
static const int8_t PA_DBM_433[8] = {-30, -20, -15, -10, -5, 0, 5, 10};
// 2FSK 发射功率表（Flipper 2fsk_async_patable，下标 0..7 由弱到强）
// 对应 dBm: -30, -20, -15, -10, 0, +5, +7, +10
static const uint8_t PA_TABLE_FM_433[8] = {0x12, 0x0E, 0x1D, 0x34, 0x60, 0x84, 0xC8, 0xC0};
// 默认配置 (RCSwitch 兼容)
static const uint8_t DEFAULT_CONFIG[][2] = {
    {CC1101_IOCFG2,     0x0D},  // GDO2 - 异步解调数据（不是载波检测）
    {CC1101_IOCFG1,     0x2E},  // GDO1 - 高阻抗
    {CC1101_IOCFG0,     0x0D},  // GDO0 - RX 异步数据 / TX 数据输入
    {CC1101_FIFOTHR,    0x47},
    {CC1101_SYNC1,      0x00},  // 禁用同步字
    {CC1101_SYNC0,      0x00},
    {CC1101_PKTLEN,     0xFF},
    {CC1101_PKTCTRL1,   0x04},
    {CC1101_PKTCTRL0,   0x32},  // 异步串行模式
    {CC1101_ADDR,       0x00},
    {CC1101_CHANNR,     0x00},
    {CC1101_FSCTRL1,    0x06},
    {CC1101_FSCTRL0,    0x00},
    {CC1101_FREQ2,      0x10},  // 433.92MHz 默认
    {CC1101_FREQ1,      0xA7},
    {CC1101_FREQ0,      0x62},
    {CC1101_MDMCFG4,    0x87},  // OOK配置
    {CC1101_MDMCFG3,    0x83},
    {CC1101_MDMCFG2,    0x30},  // ASK/OOK
    {CC1101_MDMCFG1,    0x22},
    {CC1101_MDMCFG0,    0xF8},
    {CC1101_DEVIATN,    0x15},
    {CC1101_MCSM2,      0x07},
    {CC1101_MCSM1,      0x30},
    {CC1101_MCSM0,      0x18},
    {CC1101_FOCCFG,     0x14},
    {CC1101_BSCFG,      0x6C},
    {CC1101_AGCCTRL2,   0x07},  // OOK AGC配置
    {CC1101_AGCCTRL1,   0x00},
    {CC1101_AGCCTRL0,   0x92},
    {CC1101_WOREVT1,    0x87},
    {CC1101_WOREVT0,    0x6B},
    {CC1101_WORCTRL,    0xFB},
    {CC1101_FREND1,     0x56},
    {CC1101_FREND0,     0x11},  // OOK: PATABLE[0]=关, [1]=开
    {CC1101_FSCAL3,     0xE9},
    {CC1101_FSCAL2,     0x2A},
    {CC1101_FSCAL1,     0x00},
    {CC1101_FSCAL0,     0x1F},
    {CC1101_RCCTRL1,    0x41},
    {CC1101_RCCTRL0,    0x00},
    {CC1101_FSTEST,     0x59},
    {CC1101_PTEST,      0x7F},
    {CC1101_AGCTEST,    0x3F},
    {CC1101_TEST2,      0x81},
    {CC1101_TEST1,      0x35},
    {CC1101_TEST0,      0x09},
};

CC1101Driver::CC1101Driver() {
    _initialized = false;
    _frequency = CC1101_FREQ_433MHZ;
    _dataRate = CC1101_DATARATE_2400;
    _modulation = MOD_ASK_OOK;
}

bool CC1101Driver::begin(int sck, int miso, int mosi, int cs, int gdo0, int gdo2) {
    _sck = sck;
    _miso = miso;
    _mosi = mosi;
    _cs = cs;
    _gdo0 = gdo0;
    _gdo2 = gdo2;
    
    // 初始化引脚
    pinMode(_sck, OUTPUT);
    pinMode(_mosi, OUTPUT);
    pinMode(_miso, INPUT);
    pinMode(_cs, OUTPUT);
    pinMode(_gdo0, INPUT);
    pinMode(_gdo2, INPUT);
    
    digitalWrite(_cs, HIGH);
    digitalWrite(_sck, LOW);
    
    // 初始化 SPI
    // 复位并检查连接
    if (!reset()) {
        return false;
    }
    
    if (!checkConnection()) {
        return false;
    }
    
    // 应用默认配置
    for (size_t i = 0; i < sizeof(DEFAULT_CONFIG) / sizeof(DEFAULT_CONFIG[0]); i++) {
        writeReg(DEFAULT_CONFIG[i][0], DEFAULT_CONFIG[i][1]);
    }
    
    // 写入发射功率表
    setTxPower(7);
    
    _initialized = true;
    return true;
}

void CC1101Driver::end() {
    setIdleMode();
    SPI.end();
    _initialized = false;
}

bool CC1101Driver::csLow() {
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(_cs, LOW);
    if (!waitMisoLow(10)) {
        LOG_E("CHIP_RDYn timeout: CS=%d SO=%d", _cs, _miso);
        csHigh();
        return false;
    }
    return true;
}

void CC1101Driver::csHigh() {
    digitalWrite(_cs, HIGH);
    SPI.endTransaction();
    delayMicroseconds(5);
}

uint8_t CC1101Driver::spiTransfer(uint8_t data) {
    return SPI.transfer(data);
}

bool CC1101Driver::waitMisoHigh(uint32_t timeout_ms) {
    uint32_t start = millis();
    while (gpio_get_level((gpio_num_t)_miso) == LOW) {
        if (millis() - start > timeout_ms) {
            return false;
        }
        delayMicroseconds(100);
    }
    return true;
}

bool CC1101Driver::waitMisoLow(uint32_t timeout_ms) {
    uint32_t start = millis();
    while (gpio_get_level((gpio_num_t)_miso) == HIGH) {
        if (millis() - start > timeout_ms) {
            return false;
        }
        delayMicroseconds(100);
    }
    return true;
}

bool CC1101Driver::reset() {
    // Arduino-ESP32 3.x 不允许 digitalWrite 操作已经属于 SPI 的 GPIO。
    // 先释放 SPI 做手动复位前导，再重新绑定，不能 pinMode 破坏运行中的总线。
    SPI.end();
    pinMode(_sck, OUTPUT);
    pinMode(_mosi, OUTPUT);
    pinMode(_miso, INPUT);
    pinMode(_cs, OUTPUT);
    digitalWrite(_sck, HIGH);
    digitalWrite(_mosi, LOW);
    
    // CSn 脉冲
    digitalWrite(_cs, LOW);
    delayMicroseconds(10);
    digitalWrite(_cs, HIGH);
    delayMicroseconds(40);  // 保持至少 40us [cite: 875]

    digitalWrite(_sck, LOW);
    if (!SPI.begin(_sck, _miso, _mosi, -1)) {
        LOG_E("SPI pin attach failed: SCK=%d SO=%d SI=%d", _sck, _miso, _mosi);
        return false;
    }
    
    // 拉低 CSn 并等待 MISO 变低 (CHIP_RDYn) [cite: 876]
    if (!csLow()) return false;
    
    // 发送复位选通命令 [cite: 877]
    spiTransfer(CC1101_SRES);
    
    // 等待 MISO 再次变低，表示复位完成 [cite: 877]
    bool ready = waitMisoLow(100);
    csHigh();
    if (!ready) return false;
    delay(10);
    return true;
}

bool CC1101Driver::checkConnection() {
    uint8_t partNum = readStatus(CC1101_PARTNUM);
    uint8_t version = readStatus(CC1101_VERSION);
    
    bool connected = (partNum == 0x00) && ((version == 0x04) || (version == 0x14) || (version == 0x06));
    LOG_I("SPI identity PARTNUM=0x%02X VERSION=0x%02X connected=%d", partNum, version, connected);
    return connected;
}

void CC1101Driver::writeReg(uint8_t addr, uint8_t value) {
    if (!csLow()) return;
    spiTransfer(addr);
    spiTransfer(value);
    csHigh();
}

uint8_t CC1101Driver::readReg(uint8_t addr) {
    if (!csLow()) return 0xFF;
    spiTransfer(addr | CC1101_READ_SINGLE);
    uint8_t value = spiTransfer(0);
    csHigh();
    return value;
}

void CC1101Driver::writeBurst(uint8_t addr, uint8_t *data, uint8_t length) {
    if (!csLow()) return;
    spiTransfer(addr | CC1101_WRITE_BURST);
    for (uint8_t i = 0; i < length; i++) {
        spiTransfer(data[i]);
    }
    csHigh();
}

void CC1101Driver::readBurst(uint8_t addr, uint8_t *buffer, uint8_t length) {
    if (!csLow()) { memset(buffer, 0xFF, length); return; }
    spiTransfer(addr | CC1101_READ_BURST);
    for (uint8_t i = 0; i < length; i++) {
        buffer[i] = spiTransfer(0);
    }
    csHigh();
}

void CC1101Driver::sendCommand(uint8_t cmd) {
    if (!csLow()) return;
    spiTransfer(cmd);
    csHigh();
    delayMicroseconds(5);
}

bool CC1101Driver::loadRegisterPairs(const uint8_t* pairs, bool includePaTable) {
    if (pairs == nullptr || !_initialized) return false;
    setIdleMode();
    // (reg,val) 对序列，{0,0} 结束
    const uint8_t* p = pairs;
    while (p[0] != 0 || p[1] != 0) {
        writeReg(p[0], p[1]);
        p += 2;
    }
    if (includePaTable) {
        // 结束标记后 8 字节为 PATABLE
        writeBurst(CC1101_PATABLE, const_cast<uint8_t*>(p + 2), 8);
    }
    return true;
}

uint8_t CC1101Driver::readStatus(uint8_t addr) {
    if (!csLow()) return 0xFF;
    spiTransfer(addr | CC1101_READ_BURST);
    uint8_t value = spiTransfer(0);
    csHigh();
    return value;
}

void CC1101Driver::calculateFrequencyRegs(uint32_t freq, uint8_t &freq2, uint8_t &freq1, uint8_t &freq0) {
    // FREQ = freq / (26MHz / 2^16)
    uint32_t freq_reg = ((uint64_t)freq << 16) / 26000000;
    freq2 = (freq_reg >> 16) & 0xFF;
    freq1 = (freq_reg >> 8) & 0xFF;
    freq0 = freq_reg & 0xFF;
}

bool CC1101Driver::setFrequency(uint32_t freq_hz) {
    if (freq_hz < 300000000 || freq_hz > 1000000000) {
        return false;
    }
    
    uint8_t freq2, freq1, freq0;
    calculateFrequencyRegs(freq_hz, freq2, freq1, freq0);
    
    setIdleMode();
    writeReg(CC1101_FREQ2, freq2);
    writeReg(CC1101_FREQ1, freq1);
    writeReg(CC1101_FREQ0, freq0);
    if (readReg(CC1101_FREQ2) != freq2 || readReg(CC1101_FREQ1) != freq1 ||
        readReg(CC1101_FREQ0) != freq0) {
        LOG_E("Frequency register readback failed: %luHz", (unsigned long)freq_hz);
        return false;
    }
    
    _frequency = freq_hz;
    return true;
}

bool CC1101Driver::setFrequencyMHz(float freq_mhz) {
    return setFrequency((uint32_t)(freq_mhz * 1000000));
}

uint32_t CC1101Driver::getFrequency() {
    return _frequency;
}

void CC1101Driver::calculateDataRateRegs(uint32_t baud, uint8_t &mdmcfg4, uint8_t &mdmcfg3) {
    uint8_t e;
    uint8_t m = 0;
    
    for (e = 0; e < 16; e++) {
        float m_float = ((float)baud * (1UL << 28)) / (26000000.0 * (1 << e)) - 256;
        if (m_float < 256.0 && m_float >= 0) {
            m = (uint8_t)(m_float + 0.5);
            break;
        }
    }
    
    if (e == 16) e = 15;
    
    mdmcfg4 = (readReg(CC1101_MDMCFG4) & 0xF0) | e;
    mdmcfg3 = m;
}

bool CC1101Driver::setDataRate(uint32_t baud) {
    uint8_t mdmcfg4, mdmcfg3;
    calculateDataRateRegs(baud, mdmcfg4, mdmcfg3);
    
    setIdleMode();
    writeReg(CC1101_MDMCFG4, mdmcfg4);
    writeReg(CC1101_MDMCFG3, mdmcfg3);
    
    _dataRate = baud;
    return true;
}

uint32_t CC1101Driver::getDataRate() {
    return _dataRate;
}

void CC1101Driver::setModulation(CC1101_Modulation mod) {
    uint8_t mdmcfg2 = readReg(CC1101_MDMCFG2);
    mdmcfg2 = (mdmcfg2 & ~0x70) | ((mod & 0x07) << 4);
    
    setIdleMode();
    writeReg(CC1101_MDMCFG2, mdmcfg2);
    _modulation = mod;
}

CC1101_Modulation CC1101Driver::getModulation() {
    uint8_t mdmcfg2 = readReg(CC1101_MDMCFG2);
    return (CC1101_Modulation)((mdmcfg2 >> 4) & 0x07);
}

void CC1101Driver::setTxPower(int level) {
    if (level < 0) level = 0;
    if (level > 7) level = 7;
    // OOK/ASK 整形时 PA 取 PATABLE[1]（"1"电平），[0] 固定 0x00（"0"载波关闭）。
    // 单字节 writeReg 只写 index0，旧代码 setTxPower(7)=0x0E 等于把“关断”写弱，
    // 且与 transmit 内 burst 表不一致——这是发射弱的根源之一。
    uint8_t pa[2] = {0x00, PA_TABLE_OOK_433[level]};
    writeBurst(CC1101_PATABLE, pa, 2);
}

uint8_t CC1101Driver::txPowerToPaByte(int level) {
    if (level < 0) level = 0;
    if (level > 7) level = 7;
    return PA_TABLE_OOK_433[level];
}

int CC1101Driver::txPowerToDbm(int level) {
    if (level < 0) level = 0;
    if (level > 7) level = 7;
    return PA_DBM_433[level];
}

void CC1101Driver::setChannel(uint8_t channel) {
    writeReg(CC1101_CHANNR, channel);
}

uint8_t CC1101Driver::getChannel() {
    return readReg(CC1101_CHANNR);
}

void CC1101Driver::setSyncWord(uint16_t sync) {
    setIdleMode();
    writeReg(CC1101_SYNC1, (sync >> 8) & 0xFF);
    writeReg(CC1101_SYNC0, sync & 0xFF);
    
    // 启用同步字检测
    uint8_t mdmcfg2 = readReg(CC1101_MDMCFG2);
    mdmcfg2 &= ~0x07;  // 清除同步模式位
    mdmcfg2 |= 0x02;   // 16位同步字
    writeReg(CC1101_MDMCFG2, mdmcfg2);
}

void CC1101Driver::disableSyncWord() {
    uint8_t mdmcfg2 = readReg(CC1101_MDMCFG2);
    mdmcfg2 &= ~0x07; // SYNC_MODE=000；111 是载波检测 + 30/32 同步检测
    writeReg(CC1101_MDMCFG2, mdmcfg2);
}

bool CC1101Driver::setRxMode() {
    sendCommand(CC1101_SIDLE);
    delay(1);
    sendCommand(CC1101_SRX);
    uint32_t start = millis();
    do {
        if ((getMARCState() & 0x1F) == 0x0D) return true;
        delayMicroseconds(200);
    } while (millis() - start < 25);
    LOG_E("SRX failed: MARCSTATE=0x%02X", getMARCState());
    setIdleMode();
    return false;
}

bool CC1101Driver::setTxMode() {
    sendCommand(CC1101_SIDLE);
    delay(1);
    sendCommand(CC1101_STX);
    uint32_t started = millis();
    do {
        if ((getMARCState() & 0x1F) == 0x13) return true;
        delayMicroseconds(200);
    } while ((uint32_t)(millis() - started) < 25);
    LOG_E("STX failed: MARCSTATE=0x%02X", getMARCState());
    setIdleMode();
    return false;
}

void CC1101Driver::setIdleMode() {
    sendCommand(CC1101_SIDLE);
    delay(1);
}

void CC1101Driver::enablePacketMode(uint8_t length) {
    setIdleMode();
    writeReg(CC1101_PKTLEN, length);
    
    uint8_t pktctrl0 = readReg(CC1101_PKTCTRL0);
    pktctrl0 &= ~0x03;  // 清除长度配置位
    if (length == 0) {
        pktctrl0 |= 0x02;  // 可变长度
    } else {
        pktctrl0 |= 0x00;  // 固定长度
    }
    writeReg(CC1101_PKTCTRL0, pktctrl0);
}

void CC1101Driver::disablePacketMode() {
    uint8_t pktctrl0 = readReg(CC1101_PKTCTRL0);
    pktctrl0 |= 0x02;  // 无限长度 / 异步串行
    writeReg(CC1101_PKTCTRL0, pktctrl0);
}

void CC1101Driver::setAddress(uint8_t addr) {
    writeReg(CC1101_ADDR, addr);
}

void CC1101Driver::disableAddressCheck() {
    uint8_t pktctrl1 = readReg(CC1101_PKTCTRL1);
    pktctrl1 &= ~0x03;
    writeReg(CC1101_PKTCTRL1, pktctrl1);
}

void CC1101Driver::setGDO0Output(uint8_t config) {
    writeReg(CC1101_IOCFG0, config);
}

void CC1101Driver::setGDO2Output(uint8_t config) {
    writeReg(CC1101_IOCFG2, config);
}

void CC1101Driver::flushRx() {
    setIdleMode();
    sendCommand(CC1101_SFRX);
}

void CC1101Driver::flushTx() {
    setIdleMode();
    sendCommand(CC1101_SFTX);
}

uint8_t CC1101Driver::getRxBytes() {
    return readStatus(CC1101_RXBYTES) & 0x7F;
}

uint8_t CC1101Driver::getTxBytes() {
    return readStatus(CC1101_TXBYTES) & 0x7F;
}

void CC1101Driver::writeTxFifo(uint8_t *data, uint8_t length) {
    writeBurst(CC1101_TXFIFO, data, length);
}

bool CC1101Driver::transmitRawFIFO(uint32_t dataRate, uint8_t *data, uint16_t bitCount) {
    if (bitCount == 0 || data == nullptr) return false;
    
    setIdleMode();
    
    writeReg(CC1101_MDMCFG2, 0x30);
    
    writeReg(CC1101_PKTCTRL0, 0x32);
    
    writeReg(CC1101_PKTCTRL1, 0x04);
    
    writeReg(CC1101_SYNC1, 0x00);
    writeReg(CC1101_SYNC0, 0x00);
    
    writeReg(CC1101_IOCFG0, 0x2F);
    
    writeReg(CC1101_MCSM0, 0x18);
    
    writeReg(CC1101_PATABLE, 0xC0);
    writeReg(CC1101_FREND0, 0x11);
    
    uint8_t e;
    uint8_t m = 0;
    for (e = 0; e < 16; e++) {
        float m_float = ((float)dataRate * (1UL << 28)) / (26000000.0 * (1 << e)) - 256;
        if (m_float < 256.0 && m_float >= 0) {
            m = (uint8_t)(m_float + 0.5);
            break;
        }
    }
    if (e == 16) e = 15;
    writeReg(CC1101_MDMCFG4, 0xF0 | e);
    writeReg(CC1101_MDMCFG3, m);
    
    pinMode(_gdo0, OUTPUT);
    digitalWrite(_gdo0, LOW);
    
    setTxMode();
    delayMicroseconds(1000);
    
    uint32_t bitPeriodUs = 1000000 / dataRate;
    if (bitPeriodUs < 10) bitPeriodUs = 10;
    
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);
    
    for (uint16_t i = 0; i < bitCount; i++) {
        uint16_t byteIdx = i / 8;
        uint8_t bitMask = 1 << (7 - (i % 8));
        bool bitVal = data[byteIdx] & bitMask;
        
        digitalWrite(_gdo0, bitVal ? HIGH : LOW);
        
        uint32_t start = micros();
        while (micros() - start < bitPeriodUs) {
        }
    }
    
    portEXIT_CRITICAL(&mux);
    
    digitalWrite(_gdo0, LOW);
    setIdleMode();
    pinMode(_gdo0, INPUT);
    
    return true;
}

// 直接管理IDF通道：取消时先rmt_disable，再删除encoder/通道，最后才释放波形。
// Arduino 3.3.5的rmtDeinit先删除event group/encoder，不适合中断未完成的回放。
bool CC1101Driver::initRmtTransmit() {
    rmt_tx_channel_config_t config = {};
    config.gpio_num = static_cast<gpio_num_t>(_gdo0);
    config.clk_src = RMT_CLK_SRC_DEFAULT;
    config.resolution_hz = 1000000;
    config.mem_block_symbols = 96; // ESP32-S3两块，每块48个symbol，与原配置一致
    config.trans_queue_depth = 1;
    if (rmt_new_tx_channel(&config, &_rmtTxChannel) != ESP_OK) return false;
    rmt_copy_encoder_config_t encoderConfig = {};
    if (rmt_new_copy_encoder(&encoderConfig, &_rmtEncoder) != ESP_OK ||
        rmt_enable(_rmtTxChannel) != ESP_OK) {
        if (_rmtEncoder) rmt_del_encoder(_rmtEncoder);
        rmt_del_channel(_rmtTxChannel);
        _rmtEncoder = nullptr;
        _rmtTxChannel = nullptr;
        return false;
    }
    return true;
}

bool CC1101Driver::writeRmt(rmt_data_t* items, uint32_t count) {
    if (_cancelTransmit.load()) return false;
    uint64_t durationUs = 0;
    for (uint32_t i = 0; i < count; ++i) {
        durationUs += items[i].duration0 + items[i].duration1;
    }
    const uint32_t timeoutMs = (uint32_t)((durationUs + 999) / 1000) + 500;
    rmt_transmit_config_t config = {};
    if (rmt_transmit(_rmtTxChannel, _rmtEncoder, items,
                     count * sizeof(rmt_data_t), &config) != ESP_OK) {
        LOG_E("RMT submit failed: symbols=%lu", (unsigned long)count);
        return false;
    }
    const uint32_t started = millis();
    for (;;) {
        if (_cancelTransmit.load()) return false;
        esp_err_t result = rmt_tx_wait_all_done(_rmtTxChannel, 5);
        if (result == ESP_OK) return true;
        if (result != ESP_ERR_TIMEOUT) {
            LOG_E("RMT wait failed: error=%d", (int)result);
            return false;
        }
        if ((uint32_t)(millis() - started) >= timeoutMs) {
            LOG_E("RMT timeout: symbols=%lu timeout=%lums", (unsigned long)count,
                  (unsigned long)timeoutMs);
            return false;
        }
        delay(1); // 即使RTOS tick把5ms截成0，也让出CPU并推进超时
    }
}

void CC1101Driver::finishRmtTransmit() {
    setIdleMode(); // 先关闭射频载波
    if (_rmtTxChannel) rmt_disable(_rmtTxChannel); // 停止ISR对波形和encoder的访问
    if (_rmtEncoder) rmt_del_encoder(_rmtEncoder);
    if (_rmtTxChannel) rmt_del_channel(_rmtTxChannel);
    _rmtEncoder = nullptr;
    _rmtTxChannel = nullptr;
    pinMode(_gdo0, INPUT);
}

bool CC1101Driver::transmitPT2262(const uint8_t* data, uint16_t bitCount,
                                  uint16_t shortPulse, uint16_t longPulse, uint16_t syncPulse,
                                  uint8_t repeatCount, int txPowerLevel) {
    constexpr uint16_t maxBits = 256;
    if (!data || !bitCount || bitCount > maxBits || !shortPulse || !longPulse) return false;

    uint16_t durations[2 * (maxBits + 1)];
    bool levels[2 * (maxBits + 1)];
    uint16_t count = 0;
    for (uint16_t bit = 0; bit < bitCount; ++bit) {
        const bool one = (data[bit / 8] & (0x80U >> (bit % 8))) != 0;
        durations[count] = one ? longPulse : shortPulse;
        levels[count++] = true;
        durations[count] = one ? shortPulse : longPulse;
        levels[count++] = false;
    }

    durations[count] = shortPulse;
    levels[count++] = true;
    // 每一帧用保存的同步间隔。旧条目没有同步值时保留原来的 15ms 默认间隔。
    durations[count] = syncPulse ? syncPulse : 15000;
    levels[count++] = false;
    return transmitSequence(durations, levels, nullptr, count, repeatCount, txPowerLevel,
                            TXMOD_OOK, -1, true);
}

// ─────────────────────────────────────────────────────────────────────────────
// 通用段序列发射（Flipper .sub RAW / CameStyle 协议 encoder 共用）
//
// 输入是"一帧"的 (level, duration) 段序列（level=true → GDO0 高 → 载波开），
// 内部重复 repeatCount 次，帧间不额外插 gap——协议帧头自带长 low Guard 兼做
// 帧间隔（与 Flipper encoder 语义一致）。末尾追加短 low 段保证干净收尾。
// RMT 单个 duration 上限 32767us（1MHz 分辨率 15bit），超长段拆为多个同电
// 原子段；为避免半空 item，原子段总数不足偶数时再补一个短 low 原子段。
//
// txMod 选择调制路径：
//   TXMOD_OOK       —— 原 OOK_CONFIG 整表（已验证），PATABLE={0,功率}
//   TXMOD_2FSK_*    —— 2FSK 异步整表（移植 Flipper FM 预设），PATABLE[0]=功率
// 重复缓冲：总 item ≤1024 时一次性 malloc 全部重复帧（时序最紧）；
// 否则只建单帧 item，逐次提交（避免重复缓冲占用过多内部RAM）。
// ─────────────────────────────────────────────────────────────────────────────

// 2FSK 异步发射整表（结构与 OOK_CONFIG 对齐，仅调制/AGC/前端相关寄存器不同）
static const uint8_t FM_CONFIG[47] = {
    0x0B,  // IOCFG2
    0x2E,  // IOCFG1
    0x0C,  // IOCFG0（RMT 驱动焊盘，保持与 OOK 路径一致）
    0x47,  // FIFOTHR
    0x00,  // SYNC1
    0x00,  // SYNC0
    0xFF,  // PKTLEN
    0x04,  // PKTCTRL1
    0x32,  // PKTCTRL0 异步连续
    0x00,  // ADDR
    0x00,  // CHANNR
    0x06,  // FSCTRL1
    0x00,  // FSCTRL0
    0x10,  // FREQ2（随后恢复当前频率）
    0xB0,  // FREQ1
    0x71,  // FREQ0
    0x67,  // MDMCFG4 RX BW 270kHz
    0x83,  // MDMCFG3 数据率 ~4.8kBaud
    0x04,  // MDMCFG2 2-FSK，无同步
    0x02,  // MDMCFG1
    0xF8,  // MDMCFG0
    0x04,  // DEVIATN 2.38kHz（47.6k 档发射前改写为 0x47）
    0x07,  // MCSM2
    0x30,  // MCSM1
    0x18,  // MCSM0
    0x16,  // FOCCFG
    0x6C,  // BSCFG
    0x07,  // AGCCTRL2
    0x40,  // AGCCTRL1
    0x91,  // AGCCTRL0
    0x87,  // WOREVT1
    0x6B,  // WOREVT0
    0xFB,  // WORCTRL
    0x56,  // FREND1
    0x10,  // FREND0 固定 PA（PATABLE[0]）
    0xE9,  // FSCAL3
    0x2A,  // FSCAL2
    0x00,  // FSCAL1
    0x1F,  // FSCAL0
    0x41,  // RCCTRL1
    0x00,  // RCCTRL0
    0x59,  // FSTEST
    0x7F,  // PTEST
    0x3F,  // AGCTEST
    0x81,  // TEST2
    0x35,  // TEST1
    0x09,  // TEST0
};

bool CC1101Driver::prepareRawTransmit(int presetId, int txPowerLevel) {
    uint8_t savedFreq2 = readReg(CC1101_FREQ2);
    uint8_t savedFreq1 = readReg(CC1101_FREQ1);
    uint8_t savedFreq0 = readReg(CC1101_FREQ0);
    // 与录制共用Flipper预设；清除此前扫描/固定码配置，再恢复目标频率。
    if (!reset() || !loadRegisterPairs(RfPreset(presetId).regPairs, true)) return false;
    writeReg(CC1101_FREQ2, savedFreq2);
    writeReg(CC1101_FREQ1, savedFreq1);
    writeReg(CC1101_FREQ0, savedFreq0);
    writeReg(CC1101_IOCFG2, 0x2E); // TX期间不向接收ISR输出时钟/数据
    writeReg(CC1101_IOCFG1, 0x2E);
    writeReg(CC1101_MCSM1, 0x00);
    writeReg(CC1101_CHANNR, 0x00);
    writeReg(CC1101_FSCTRL0, 0x00);
    bool fm = RfPreset(presetId).isFM;
    uint8_t power = fm ? PA_TABLE_FM_433[txPowerLevel] : PA_TABLE_OOK_433[txPowerLevel];
    uint8_t pa[8] = {};
    pa[fm ? 0 : 1] = power;
    writeBurst(CC1101_PATABLE, pa, 8);
    // 写入成功才能启动波形，RMT完成不能代替射频芯片状态校验。
    for (const uint8_t* pair = RfPreset(presetId).regPairs; pair[0] || pair[1]; pair += 2) {
        if (readReg(pair[0]) != pair[1]) {
            LOG_E("RAW TX register mismatch: addr=0x%02X", pair[0]);
            return false;
        }
    }
    uint8_t actualPa[8];
    readBurst(CC1101_PATABLE, actualPa, 8);
    if (memcmp(actualPa, pa, sizeof(pa)) != 0 || readReg(CC1101_MCSM1) != 0 ||
        readReg(CC1101_CHANNR) != 0 || readReg(CC1101_FSCTRL0) != 0) return false;
    if (readReg(CC1101_FREQ2) != savedFreq2 || readReg(CC1101_FREQ1) != savedFreq1 ||
        readReg(CC1101_FREQ0) != savedFreq0) return false;
    return true;
}

bool CC1101Driver::transmitPulses(const uint16_t* durations, const bool* levels,
                                  uint16_t count, uint8_t repeatCount, int txPowerLevel,
                                  uint8_t txMod) {
    return transmitSequence(durations, levels, nullptr, count, repeatCount, txPowerLevel, txMod, -1);
}

bool CC1101Driver::transmitRaw(const int32_t* pulses, uint16_t count, int txPowerLevel, uint8_t presetId) {
    if (presetId >= RF_PRESET_COUNT) return false;
#if RF_RAW_TX_USE_RMT
    return transmitSequence(nullptr, nullptr, pulses, count, 1, txPowerLevel, TXMOD_OOK, presetId);
#else
    if (!pulses || !count) return false;
    struct Source { const int32_t* pulses; uint16_t count, index; } source{pulses,count,0};
    auto read=[](void* context,int32_t& value)->int {
        auto* source=static_cast<Source*>(context);
        if (source->index==source->count) return 0;
        value=source->pulses[source->index++];
        return 1;
    };
    return transmitRawGpioStream(read,&source,txPowerLevel,presetId);
#endif
}

bool CC1101Driver::transmitSequence(const uint16_t* durations, const bool* levels, const int32_t* raw,
                                  uint16_t count, uint8_t repeatCount, int txPowerLevel,
                                  uint8_t txMod, int presetId, bool continuousRepeats) {
    if ((!raw && (!durations || !levels)) || count == 0) return false;
    auto durationAt = [&](uint16_t i) -> uint32_t {
        int64_t v = raw ? raw[i] : durations[i];
        return (uint32_t)(v < 0 ? -v : v);
    };
    auto levelAt = [&](uint16_t i) { return raw ? raw[i] > 0 : levels[i]; };
    if (repeatCount == 0) repeatCount = 1;
    if (txPowerLevel < 0) txPowerLevel = 0;
    if (txPowerLevel > 7) txPowerLevel = 7;

    setIdleMode();

    // 保存当前频率（OOK/FM 配置表都含硬编码频点，写表后必须恢复，
    // 否则 315MHz 等其他频点发射会错误地出在 433.92MHz）
    uint8_t savedFreq2 = readReg(CC1101_FREQ2);
    uint8_t savedFreq1 = readReg(CC1101_FREQ1);
    uint8_t savedFreq0 = readReg(CC1101_FREQ0);

    if (presetId >= 0) {
        if (!prepareRawTransmit(presetId, txPowerLevel)) return false;
    } else if (txMod == TXMOD_OOK) {
        // ── OOK 异步发射配置（已验证整表）──
        static const uint8_t OOK_CONFIG[] = {
           0x0B,  0x2E,  0x0C,  0x47,  0x00,  0x00,  0xFF,  0x04,
           0x32,  0x00,  0x00,  0x06,  0x00,  0x10,  0xB0,  0x71,
           0xF5,  0x83,  0x30,  0x22,  0xF8,  0x15,  0x07,  0x30,
           0x18,  0x14,  0x6C,  0x03,  0x40,  0x92,  0x87,  0x6B,
           0xFB,  0x56,  0x11,  0xE9,  0x2A,  0x00,  0x1F,  0x41,
           0x00,  0x59,  0x7F,  0x3F,  0x81,  0x35,  0x09,
        };
        for (size_t i = 0; i < sizeof(OOK_CONFIG); i++) {
            writeReg(i, OOK_CONFIG[i]);
        }
        writeReg(CC1101_FREQ2, savedFreq2);
        writeReg(CC1101_FREQ1, savedFreq1);
        writeReg(CC1101_FREQ0, savedFreq0);

        // PA 功率表（OOK/ASK：index0=载波关闭，index1=发射功率）
        uint8_t paTable[8] = {0x00, PA_TABLE_OOK_433[txPowerLevel], 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x00};
        writeBurst(CC1101_PATABLE, paTable, 8);
        writeReg(CC1101_FREND0, 0x11);
    } else {
        // ── 2FSK 异步发射配置（移植 Flipper FM 预设）──
        uint8_t fmRegs[sizeof(FM_CONFIG)];
        memcpy(fmRegs, FM_CONFIG, sizeof(FM_CONFIG));
        fmRegs[CC1101_DEVIATN] = (txMod == TXMOD_2FSK_47K6) ? 0x47 : 0x04;
        for (size_t i = 0; i < sizeof(fmRegs); i++) writeReg(i, fmRegs[i]);

        writeReg(CC1101_FREQ2, savedFreq2);
        writeReg(CC1101_FREQ1, savedFreq1);
        writeReg(CC1101_FREQ0, savedFreq0);

        // 2FSK：载波常发，PATABLE[0]=功率，FREND0=0x10（表内已设）
        uint8_t paTable[8] = {PA_TABLE_FM_433[txPowerLevel], 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x00};
        writeBurst(CC1101_PATABLE, paTable, 8);
        writeReg(CC1101_FREND0, 0x10);
    }

    // ── 原子段展开（两遍法：先数后填，避免中间缓冲）──
    static const uint16_t RMT_MAX_DUR = 32767;
    uint32_t atomsPerFrame = 0;
    for (uint16_t i = 0; i < count; i++) {
        uint32_t d = durationAt(i);
        if (d == 0) continue;  // 0 时长段跳过（无物理意义）
        atomsPerFrame += (d + RMT_MAX_DUR - 1) / RMT_MAX_DUR;  // ceil
    }
    if (atomsPerFrame == 0) return false;

    // 初始化 RMT（1MHz 分辨率 = 1us 精度）
    if (!initRmtTransmit()) {
        return false;
    }

    auto emitAtom = [](rmt_data_t* items, uint32_t& a, bool level, uint32_t durUs) {
        rmt_data_t& item = items[a >> 1];
        if ((a & 1) == 0) {
            item.level0 = level ? 1 : 0;
            item.duration0 = (uint16_t)durUs;
        } else {
            item.level1 = level ? 1 : 0;
            item.duration1 = (uint16_t)durUs;
        }
        a++;
    };

    auto buildFrame = [&](rmt_data_t* items, uint32_t& atomCount) {
        uint32_t a = 0;
        for (uint16_t i = 0; i < count; i++) {
            uint32_t d = durationAt(i);
            if (d == 0) continue;
            bool lv = levelAt(i);
            while (d > 0) {
                uint32_t chunk = (d > RMT_MAX_DUR) ? RMT_MAX_DUR : d;
                emitAtom(items, a, lv, chunk);
                d -= chunk;
            }
        }
        // 收尾 low（+ 偶数化补段）：保证发射以载波关断结束
        emitAtom(items, a, false, 1000);
        if (a & 1) emitAtom(items, a, false, 1000);
        atomCount = a;
    };

    // 一次性重复的 item 上限（约 4KB 缓冲）；超限走单帧软件循环
    static const uint32_t WHOLE_ITEM_LIMIT = 1024;
    uint32_t tailAtoms = ((atomsPerFrame + 2) & ~1u);  // 含收尾的偶数化原子数
    bool ok = false;
    bool attempted = false;

    if (continuousRepeats || tailAtoms * repeatCount / 2 <= WHOLE_ITEM_LIMIT) {
        // ── 全部重复帧装入一个缓冲（帧间零间隔，时序最紧，原行为）──
        uint32_t totalAtoms = atomsPerFrame * repeatCount + (continuousRepeats ? 0 : 1);
        if (totalAtoms & 1) totalAtoms++;
        uint32_t rmtItemsCount = totalAtoms / 2;
        rmt_data_t* rmtData = (rmt_data_t*)heap_caps_malloc(rmtItemsCount * sizeof(rmt_data_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (rmtData != nullptr) {
            uint32_t a = 0;
            for (uint8_t rep = 0; rep < repeatCount; rep++) {
                for (uint16_t i = 0; i < count; i++) {
                    uint32_t d = durationAt(i);
                    if (d == 0) continue;
                    bool lv = levelAt(i);
                    while (d > 0) {
                        uint32_t chunk = (d > RMT_MAX_DUR) ? RMT_MAX_DUR : d;
                        emitAtom(rmtData, a, lv, chunk);
                        d -= chunk;
                    }
                }
            }
            if (!continuousRepeats) emitAtom(rmtData, a, false, 1000);
            // 固定码以实测同步低电平结束，只在最后一个 RMT 符号半空时补 1us。
            if (a < totalAtoms) emitAtom(rmtData, a, false, continuousRepeats ? 1 : 1000);

            attempted = true;
            ok = setTxMode();
            if (ok) {
                delayMicroseconds(500);
                ok = writeRmt(rmtData, a >> 1);
            }
            finishRmtTransmit();
            free(rmtData);
        }
    }

    if (!attempted) {
        if (continuousRepeats) {
            // 固定码连续提交，避免拆成软件循环时插入额外帧间延时。
            LOG_E("RMT fixed-code buffer allocation failed");
            finishRmtTransmit();
            return false;
        }
        // ── 单帧缓冲 + 软件循环发射（大 RAW 帧兜底，内存占用 ≤ 单帧大小）──
        uint32_t itemsCap = (tailAtoms / 2) + 1;
        rmt_data_t* frameData = (rmt_data_t*)heap_caps_malloc(itemsCap * sizeof(rmt_data_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (frameData == nullptr) {
            finishRmtTransmit();
            return false;
        }

        uint32_t atomCount = 0;
        buildFrame(frameData, atomCount);

        ok = setTxMode();
        if (ok) delayMicroseconds(500);
        for (uint8_t rep = 0; ok && rep < repeatCount; rep++) {
            if (!writeRmt(frameData, atomCount >> 1)) {
                ok = false;
                break;
            }
        }
        finishRmtTransmit();
        free(frameData);
    }

    return ok;
}

static size_t IRAM_ATTR rawStreamEncode(const void*, size_t, size_t, size_t freeSymbols,
                                        rmt_symbol_word_t* symbols, bool* done, void* context) {
    return static_cast<RawRmtStream*>(context)->consume(symbols, freeSymbols, done);
}

bool CC1101Driver::transmitRawStream(RawReadCallback read, void* context,
                                    int txPowerLevel, uint8_t presetId) {
    if (!read || presetId >= RF_PRESET_COUNT) return false;
    txPowerLevel = max(0, min(7, txPowerLevel));
    void* memory = heap_caps_malloc(sizeof(RawRmtStream), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!memory) return false;
    auto* ring = new(memory) RawRmtStream;
    uint32_t remaining = 0, samples = 0;
    uint64_t durationUs = 0;
    bool level = false, eof = false, readError = false, tailAdded = false;
    auto atom = [&](uint16_t& duration, bool& atomLevel) -> bool {
        while (!remaining) {
            if (eof) {
                if (tailAdded) return false;
                tailAdded = true;
                remaining = 1000;
                level = false;
                break;
            }
            int32_t pulse = 0;
            int status = read(context, pulse);
            if (status < 0) { readError = true; return false; }
            if (!status) { eof = true; continue; }
            ++samples;
            int64_t v = pulse;
            remaining = (uint32_t)(v < 0 ? -v : v);
            level = pulse > 0;
        }
        duration = (uint16_t)min(remaining, (uint32_t)32767);
        remaining -= duration;
        atomLevel = level;
        durationUs += duration;
        return true;
    };
    auto refill = [&]() {
        while (ring->freeSpace() && !ring->isFinished() && !readError && !_cancelTransmit.load()) {
            uint16_t d0, d1; bool l0, l1;
            if (!atom(d0, l0)) { ring->finish(); break; }
            if (!atom(d1, l1)) {
                d1 = 1000; l1 = false; durationUs += d1;
                // 第一半段仍要发送，再发布EOF。
                rmt_data_t item = {};
                item.duration0 = d0; item.level0 = l0;
                item.duration1 = d1; item.level1 = l1;
                ring->push(item);
                ring->finish();
                break;
            }
            rmt_data_t item = {};
            item.duration0 = d0; item.level0 = l0;
            item.duration1 = d1; item.level1 = l1;
            ring->push(item);
        }
    };
    refill(); // 先预读，避免刚启动就因存储延迟断供；不发射无效/过短文件。
    bool initialized = false;
    bool ok = !readError && samples > 0 && !_cancelTransmit.load() &&
              prepareRawTransmit(presetId, txPowerLevel) && initRmtTransmit();
    if (ok) {
        initialized = true;
        // 用连续回调编码器替换copy encoder；整份文件只提交一次RMT事务。
        rmt_del_encoder(_rmtEncoder);
        _rmtEncoder = nullptr;
        rmt_simple_encoder_config_t encoder = {};
        encoder.callback = rawStreamEncode;
        encoder.arg = ring;
        encoder.min_chunk_size = 1;
        ok = rmt_new_simple_encoder(&encoder, &_rmtEncoder) == ESP_OK && setTxMode();
    }
    uint8_t marker = 0;
    uint32_t started = millis();
    if (ok) {
        rmt_transmit_config_t config = {};
        ok = rmt_transmit(_rmtTxChannel, _rmtEncoder, &marker, sizeof(marker), &config) == ESP_OK;
    }
    while (ok) {
        if (_cancelTransmit.load() || readError || ring->hasUnderrun()) { ok = false; break; }
        // 小于一半时补充，由任务读取LittleFS；ISR始终只取内部RAM。
        if (ring->freeSpace() >= RawRmtStream::Capacity / 2) refill();
        if (readError || ring->hasUnderrun()) { ok = false; break; }
        esp_err_t result = rmt_tx_wait_all_done(_rmtTxChannel, 2);
        if (result == ESP_OK) {
            ok = ring->isFinished() && !ring->hasUnderrun();
            break;
        }
        if (result != ESP_ERR_TIMEOUT ||
            (uint64_t)(uint32_t)(millis() - started) > durationUs / 1000 + 500) {
            ok = false;
            break;
        }
        delay(1);
    }
    LOG_I("[TX.raw] stream samples=%lu duration=%lluus symbols=%lu ok=%d read_error=%d underrun=%d",
          (unsigned long)samples, (unsigned long long)durationUs,
          (unsigned long)ring->consumed(), ok, readError, ring->hasUnderrun());
    if (initialized) finishRmtTransmit();
    ring->~RawRmtStream();
    free(memory);
    return ok;
}

bool CC1101Driver::transmitRawGpioStream(RawReadCallback read, void* context,
                                        int txPowerLevel, uint8_t presetId) {
    if (!read || presetId>=RF_PRESET_COUNT) return false;
    txPowerLevel=max(0,min(7,txPowerLevel));
    void* memory=heap_caps_malloc(sizeof(RawGpioStream),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if (!memory) return false;
    auto* queue=new(memory) RawGpioStream(static_cast<gpio_num_t>(_gdo0));
    uint32_t samples=0;
    uint64_t durationUs=0;
    bool eof=false,readError=false,shortPulse=false;
    auto refill=[&]() {
        while (queue->freeSpace() && !eof && !readError && !_cancelTransmit.load()) {
            int32_t pulse=0;
            int status=read(context,pulse);
            if (status<0) { readError=true;break; }
            if (!status) {
                queue->push(-1000); // 结束时保持载波关，和原RAW回放一致
                durationUs+=1000;eof=true;queue->finish();break;
            }
            if (!pulse) continue;
            ++samples;
            int64_t value=pulse;
            uint32_t duration=(uint32_t)(value<0 ? -value : value);
            if (duration<10) { readError=shortPulse=true;break; }
            queue->push(pulse);
            durationUs+=duration;
        }
    };
    refill();
    gptimer_handle_t timer=nullptr;
    bool enabled=false,started=false,gpioAttached=false;
    bool ok=!readError && samples>0 && !_cancelTransmit.load() && prepareRawTransmit(presetId,txPowerLevel);
    if (ok) {
        // 此路径不创建/使用RMT。Arduino GPIO归属明确设为普通输出。
        pinMode(_gdo0,OUTPUT);
        gpioAttached=true;
        ok=gpio_set_level(static_cast<gpio_num_t>(_gdo0),0)==ESP_OK;
    }
    if (ok) {
        gptimer_config_t config={};
        config.clk_src=GPTIMER_CLK_SRC_DEFAULT;
        config.direction=GPTIMER_COUNT_UP;
        config.resolution_hz=1000000;
        config.intr_priority=3;
        ok=gptimer_new_timer(&config,&timer)==ESP_OK;
    }
    if (ok) {
        gptimer_event_callbacks_t callbacks={};callbacks.on_alarm=RawGpioStream::alarm;
        ok=gptimer_register_event_callbacks(timer,&callbacks,queue)==ESP_OK;
    }
    if (ok) { ok=gptimer_enable(timer)==ESP_OK;enabled=ok; }
    if (ok) ok=setTxMode();
    if (ok) {
        gptimer_alarm_config_t first={};first.alarm_count=20;
        ok=gptimer_set_raw_count(timer,0)==ESP_OK && gptimer_set_alarm_action(timer,&first)==ESP_OK;
    }
    if (ok) { ok=gptimer_start(timer)==ESP_OK;started=ok; }
    uint32_t start=millis();
    while (ok) {
        auto state=queue->snapshot();
        if (_cancelTransmit.load() || readError || state.underrun || state.late || state.timerError) {ok=false;break;}
        if (state.done) break;
        if (queue->freeSpace()>=RawGpioStream::Capacity/2) refill();
        if ((uint64_t)(uint32_t)(millis()-start)>durationUs/1000+500) {ok=false;break;}
        delay(1);
    }
    setIdleMode(); // 先关闭载波，停止定时器/ISR后再释放队列
    if (started) gptimer_stop(timer);
    if (enabled) gptimer_disable(timer);
    if (timer) gptimer_del_timer(timer);
    if (gpioAttached) { gpio_set_level(static_cast<gpio_num_t>(_gdo0),0);pinMode(_gdo0,INPUT); }
    auto state=queue->snapshot();
    ok=ok && state.done && !readError && !state.underrun && !state.late && !state.timerError && !_cancelTransmit.load();
    LOG_I("[TX.raw] GPIO/GPTimer samples=%lu duration=%lluus emitted=%lu ok=%d read_error=%d underrun=%d late=%d max_late=%luus timer_error=%d short=%d",
          (unsigned long)samples,(unsigned long long)durationUs,(unsigned long)state.emitted,ok,
          readError,state.underrun,state.late,(unsigned long)state.maxLateUs,state.timerError,shortPulse);
    queue->~RawGpioStream();free(memory);
    return ok;
}

bool CC1101Driver::sendPacket(uint8_t *data, uint8_t length) {
    if (length > 63) return false;
    
    setIdleMode();
    flushTx();
    
    // 写入长度和数据
    writeReg(CC1101_TXFIFO, length);
    writeBurst(CC1101_TXFIFO, data, length);
    
    setTxMode();
    
    // 等待发送完成
    uint32_t start = millis();
    while (getTxBytes() > 0) {
        if (millis() - start > 1000) {
            setIdleMode();
            return false;
        }
        delay(1);
    }
    
    setIdleMode();
    return true;
}

int CC1101Driver::receivePacket(uint8_t *buffer, uint8_t maxLength) {
    uint8_t rxBytes = getRxBytes();
    if (rxBytes == 0) return 0;
    
    // 读取长度
    uint8_t length = readReg(CC1101_RXFIFO);
    rxBytes--;
    
    if (length > maxLength) length = maxLength;
    if (length > rxBytes) length = rxBytes;
    
    // 读取数据
    for (uint8_t i = 0; i < length; i++) {
        buffer[i] = readReg(CC1101_RXFIFO);
    }
    
    // 读取 RSSI 和 LQI
    readReg(CC1101_RXFIFO);  // RSSI
    uint8_t lqi = readReg(CC1101_RXFIFO);
    
    if (lqi & 0x80) {  // CRC OK
        return length;
    }
    
    return -1;  // CRC 错误
}

uint8_t CC1101Driver::readRSSI() {
    return readStatus(CC1101_RSSI);
}

int16_t CC1101Driver::getRSSIdBm() {
    uint8_t rssi_dec = readStatus(CC1101_RSSI);
    int16_t rssi_dbm;
    uint8_t rssi_offset = 74; // 针对 433MHz 的典型补偿值 
    
    // 按照数据手册进行转换 [cite: 786, 789]
    if (rssi_dec >= 128) {
        rssi_dbm = ((int16_t)rssi_dec - 256) / 2 - rssi_offset;
    } else {
        rssi_dbm = (rssi_dec / 2) - rssi_offset;
    }
    return rssi_dbm;
}

uint8_t CC1101Driver::readLQI() {
    return readStatus(CC1101_LQI) & 0x7F;
}

uint8_t CC1101Driver::getMARCState() {
    return readStatus(CC1101_MARCSTATE);
}

void CC1101Driver::enableCarrierSense() {
    setGDO2Output(0x0E);  // Carrier Sense；0x0D 是异步数据
}

void CC1101Driver::disableCarrierSense() {
    setGDO2Output(0x2E);  // 高阻抗；0x2F 是硬件低电平
}

bool CC1101Driver::carrierDetected() {
    return digitalRead(_gdo2) == HIGH;
}

void CC1101Driver::printRegisters() {
    LOG_D("\n=== CC1101 Registers ===");
    LOG_D("FREQ: 0x%02X%02X%02X (%lu Hz)",
          readReg(CC1101_FREQ2), readReg(CC1101_FREQ1), readReg(CC1101_FREQ0),
          _frequency);
    LOG_D("MDMCFG: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X",
          readReg(CC1101_MDMCFG4), readReg(CC1101_MDMCFG3),
          readReg(CC1101_MDMCFG2), readReg(CC1101_MDMCFG1),
          readReg(CC1101_MDMCFG0));
    LOG_D("PKTCTRL: 0x%02X 0x%02X",
          readReg(CC1101_PKTCTRL1), readReg(CC1101_PKTCTRL0));
    LOG_D("RSSI: %d dBm", getRSSIdBm());
    LOG_D("MARCSTATE: 0x%02X", getMARCState());
    LOG_D("========================");
}

void CC1101Driver::restoreDefaultConfig() {
    setIdleMode();
    
    uint8_t savedFreq2 = readReg(CC1101_FREQ2);
    uint8_t savedFreq1 = readReg(CC1101_FREQ1);
    uint8_t savedFreq0 = readReg(CC1101_FREQ0);
    
    for (size_t i = 0; i < sizeof(DEFAULT_CONFIG) / sizeof(DEFAULT_CONFIG[0]); i++) {
        writeReg(DEFAULT_CONFIG[i][0], DEFAULT_CONFIG[i][1]);
    }
    
    writeReg(CC1101_FREQ2, savedFreq2);
    writeReg(CC1101_FREQ1, savedFreq1);
    writeReg(CC1101_FREQ0, savedFreq0);
    
    _modulation = MOD_ASK_OOK;
    setTxPower(7);
    
    pinMode(_gdo0, INPUT);
    pinMode(_gdo2, INPUT);
}
