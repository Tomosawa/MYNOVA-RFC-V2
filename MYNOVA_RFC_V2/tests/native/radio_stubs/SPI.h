#pragma once
#include <Arduino.h>
#define SPI_MODE0 0
#define MSBFIRST 1
struct SPISettings { SPISettings(uint32_t f, int order, int mode) { assert(f==1000000 && order==MSBFIRST && mode==SPI_MODE0); } };
struct RadioSPI {
    std::array<uint8_t, 64> regs{};
    std::array<uint8_t, 8> pa{};
    bool selected=false, active=false, transaction=false, header=true;
    bool read=false, burst=false, stuckReady=false, failRx=false, failTx=false;
    int addr=0, paIndex=0, sck=-1, miso=-1, mosi=-1, transfers=0, tuneCount=0;
    int dropWriteAddr=-1;
    uint32_t failRxFrequencyWord=0;
    uint8_t marc=1, rssi=184; // -110 dBm
    RadioSPI() { radioSelect = selectThunk; }
    static void selectThunk(bool low);
    void select(bool low) {
        selected=low; header=true; paIndex=0;
        pinLevels[41]=low && !stuckReady ? LOW : HIGH;
    }
    bool begin(int s, int i, int o, int ss) {
        assert(ss==-1); sck=s;miso=i;mosi=o;
        active=true;spiPins[s]=spiPins[i]=spiPins[o]=true;return true;
    }
    void end() { if(active) { spiPins[sck]=spiPins[miso]=spiPins[mosi]=false; } active=false; }
    void beginTransaction(SPISettings) { assert(active && !transaction);transaction=true; }
    void endTransaction() { assert(transaction);transaction=false; }
    uint8_t transfer(uint8_t v) {
        assert(active && selected && transaction && pinLevels[41]==LOW);
        transfers++;
        if(header) {
            header=false;read=v&0x80;burst=v&0x40;addr=v&0x3f;
            if(!read && addr>=0x30 && addr<=0x3d) {
                if(addr==0x30) { regs.fill(0);pa.fill(0);marc=1; }
                if(addr==0x34) {
                    uint32_t word=(regs[0x0d]<<16)|(regs[0x0e]<<8)|regs[0x0f];
                    marc=failRx || word==failRxFrequencyWord ? 1 : 0x0d;
                }
                if(addr==0x36) marc=1;
                if(addr==0x35) marc=failTx ? 1 : 0x13;
            }
            return 0;
        }
        uint8_t result=0;
        if(read) {
            if(addr==0x30) result=0;
            else if(addr==0x31) result=0x14;
            else if(addr==0x35) result=marc;
            else if(addr==0x34) result=rssi;
            else if(addr==0x3e) result=pa[paIndex++];
            else result=regs[addr];
        } else {
            if(addr==0x3e) pa[paIndex++]=v;
            else { if(addr!=dropWriteAddr) regs[addr]=v;if(addr==0x0f) tuneCount++; }
        }
        if(burst && addr!=0x3e) addr++;
        return result;
    }
};
inline RadioSPI SPI;
inline void RadioSPI::selectThunk(bool low) { SPI.select(low); }
