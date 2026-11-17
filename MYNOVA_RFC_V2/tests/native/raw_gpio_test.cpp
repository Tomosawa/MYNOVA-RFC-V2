#include "CC1101Driver.h"
#include "RawGpioStream.h"
#include "RFTransmitConfig.h"
#include "RawSubGhzReader.h"
#include "RFSubGhzPresets.h"
#include <esp_heap_caps.h>
#include <iostream>
static_assert(RF_RAW_TX_USE_RMT==0);
static CC1101Driver radio;
static void reset() {
    assert(!mockTimer && !rmtActive);
    gpioEdges.clear();rmtWriteCalls=0;timerFailAt=timerApiCalls=timerAlarmCalls=0;
    timerLateUs=0;timerNoInterrupt=false;timerPollHook=nullptr;failRmtAlloc=false;
    radio.resetTransmitCancel();
}
static void clean() {assert(!mockTimer && !radioAdvanceHook && !rmtActive && rmtWriteCalls==0 && SPI.marc==1 && pinModes[39]==INPUT);}
static void add(std::vector<int64_t>& out,int64_t v) {
    if(!v)return;
    if(!out.empty() && (out.back()>0)==(v>0))out.back()+=v;else out.push_back(v);
}
static void compare(const std::vector<int32_t>& source) {
    std::vector<int64_t> expected,actual;
    for(auto v:source)add(expected,v);
    add(expected,-1000);
    std::vector<GpioEdge> edges;for(auto e:gpioEdges)if(e.tx)edges.push_back(e);
    assert(edges.size()==source.size()+2); // all samples, guard, then EOF carrier-off
    for(size_t i=0;i+1<edges.size();++i) {
        auto duration=(int64_t)(edges[i+1].time-edges[i].time);
        add(actual,edges[i].level ? duration : -duration);
    }
    assert(actual==expected);
}
struct Source {std::vector<int32_t> pulses;size_t index=0;int failAfter=-1;bool slow=false;};
static int read(void* ctx,int32_t& pulse) {
    assert(!radioInISR);auto& s=*static_cast<Source*>(ctx);
    if(s.failAfter>=0 && s.index==(size_t)s.failAfter)return -1;
    if(s.slow && s.index==RawGpioStream::Capacity+1)delay(150);
    if(s.index==s.pulses.size())return 0;
    pulse=s.pulses[s.index++];return 1;
}
static int fileRead(void* ctx,int32_t& pulse) {assert(!radioInISR);return RawSubGhzReader::read(ctx,pulse);}
int main() {
    assert(radio.begin(42,41,2,38,39,40));
    fs::FS files;
    std::vector<int32_t> continuous(70000);
    for(size_t i=0;i<continuous.size();++i)continuous[i]=(i%2) ? (i%101==0 ? -130000 : -66) : 33;
    for(uint8_t preset=0;preset<RF_PRESET_COUNT;++preset) {
        std::string content="Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: 433920000\nPreset: ";
        content+=RfPreset(preset).flipperName;content+="\nProtocol: RAW\n";
        for(size_t i=0;i<continuous.size();++i) {
            if(i%256==0)content+="RAW_Data: ";
            content+=std::to_string(continuous[i]);content+=(i%256==255 || i+1==continuous.size()) ? '\n' : ' ';
        }
        testFiles["/test.sub"]=std::make_shared<std::string>(content);
        RawSubGhzReader reader;assert(reader.open(files,"/test.sub"));
        assert(radio.setFrequency(reader.frequencyHz));
        const std::array<uint8_t,3> freq{SPI.regs[0x0d],SPI.regs[0x0e],SPI.regs[0x0f]};
        reset();assert(radio.transmitRawGpioStream(fileRead,&reader,7,preset));
        assert(reader.count==continuous.size());compare(continuous);clean();
        assert((std::array<uint8_t,3>{SPI.regs[0x0d],SPI.regs[0x0e],SPI.regs[0x0f]})==freq);
        assert(SPI.regs[CC1101_MDMCFG3]==(preset<RF_PRESET_FM238 ? 0x32 : 0x83));
    }
    std::vector<int32_t> pulse{33,-66,33,-90000,66,-33,66,-130000};
    reset();assert(radio.transmitRaw(pulse.data(),pulse.size(),7,0));compare(pulse);clean();
    // Every setup operation and the first ISR count/rearm failure must stop RF and release timer.
    for(int fail=1;fail<=8;++fail) {
        reset();timerFailAt=fail;Source s{pulse};assert(!radio.transmitRawGpioStream(read,&s,7,0));clean();
    }
    reset();timerLateUs=3;Source jitter{pulse};
    assert(radio.transmitRawGpioStream(read,&jitter,7,0));compare(pulse);clean();
    reset();timerLateUs=9;Source edgeLate{pulse};
    assert(!radio.transmitRawGpioStream(read,&edgeLate,7,0));clean();
    reset();Source empty{{0,0}};assert(!radio.transmitRawGpioStream(read,&empty,7,0));clean();
    reset();timerLateUs=25;Source late{pulse};assert(!radio.transmitRawGpioStream(read,&late,7,0));clean();
    reset();timerNoInterrupt=true;Source lost{pulse};assert(!radio.transmitRawGpioStream(read,&lost,7,0));clean();
    reset();timerPollHook=[](){radio.cancelTransmit();};Source cancel{continuous};
    assert(!radio.transmitRawGpioStream(read,&cancel,7,0));clean();
    reset();Source error{continuous,0,2200};assert(!radio.transmitRawGpioStream(read,&error,7,0));clean();
    reset();Source starved{std::vector<int32_t>(10000,33),0,-1,true};
    assert(!radio.transmitRawGpioStream(read,&starved,7,0));clean();
    reset();Source tooShort{{9,-33}};assert(!radio.transmitRawGpioStream(read,&tooShort,7,0));clean();
    reset();SPI.failTx=true;Source noTx{pulse};assert(!radio.transmitRawGpioStream(read,&noTx,7,0));clean();SPI.failTx=false;
    reset();failRmtAlloc=true;Source oom{pulse};assert(!radio.transmitRawGpioStream(read,&oom,7,0));clean();
    reset();assert(radio.transmitRaw(pulse.data(),pulse.size(),7,0));compare(pulse);clean();
    std::cout<<"RAW GPIO: 70000 segments x 4 presets exact duration/polarity; long gaps, no RMT, cancel, read/underflow, timer errors, late IRQ, timeout, OOM and retry PASS\n";
}
