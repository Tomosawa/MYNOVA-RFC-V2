#include "CC1101Driver.h"
#include "RFSignalDecoder.h"
#include "FlipperSignalFile.h"
#include "RFSubGhzPresets.h"
#include "RawSubGhzReader.h"
#include "RawRmtStream.h"
#include <iostream>
#include <vector>
#include <memory>

static CC1101Driver* stopTarget=nullptr;
static void addRun(std::vector<int64_t>& values,int64_t value) {
    if (!value) return;
    if (!values.empty() && (values.back()>0)==(value>0)) values.back()+=value;
    else values.push_back(value);
}
static std::vector<int64_t> outputRuns() {
    std::vector<int64_t> runs;
    for(auto& transaction:rmtWrites) for(auto symbol:transaction) {
        addRun(runs,symbol.level0 ? symbol.duration0 : -(int64_t)symbol.duration0);
        addRun(runs,symbol.level1 ? symbol.duration1 : -(int64_t)symbol.duration1);
    }
    return runs;
}
static void compare(const std::vector<int32_t>& input) {
    std::vector<int64_t> expected;
    for(auto v:input) addRun(expected,v);
    auto sent=outputRuns();
    assert(sent.size()==expected.size());
    // The only extra duration is the explicit final carrier-off guard.
    auto extra=expected.back()-sent.back();
    assert(extra==1000 || extra==2000);
    sent.back()=expected.back();
    assert(sent==expected); // Every polarity, transition and intermediate gap must match.
}
static void drain(RFSignalDecoder& decoder,std::vector<int32_t>& pulses) {
    RawSeg raw[128];uint16_t n;
    while((n=decoder.rawDrain(raw,128))) for(uint16_t i=0;i<n;++i)
        pulses.push_back(raw[i].level ? raw[i].durUs : -(int32_t)raw[i].durUs);
}
int main() {
    fs::FS files;
    RFSignalDecoder decoder;
    decoder.begin(39,40);decoder.enableRawMode(true);
    radioClockUs+=1000000;pinLevels[40]=HIGH;decoder.processInterrupt();
    std::vector<int32_t> captured, original;
    // Forty complete 24-bit frames, using short 33/66us symbols (> old 1024 limit).
    for(int repeat=0;repeat<40;++repeat) {
        for(int bit=23;bit>=0;--bit) {
            bool one=(0x97b628u>>bit)&1;
            original.push_back(one ? 66 : 33);
            original.push_back(one ? -33 : -66);
        }
        original.push_back(33);original.push_back(-130000); // Gap must survive split/file/replay.
    }
    for(auto v:original) {
        radioClockUs += v>0 ? v : -v; pinLevels[40]=v>0 ? LOW : HIGH;
        decoder.processInterrupt();drain(decoder,captured);
    }
    decoder.enableRawMode(false);drain(decoder,captured);
    std::vector<int64_t> a,b;
    for(auto v:original) addRun(a,v);
    for(auto v:captured) addRun(b,v);
    assert(a==b && captured.size()>1024 && !decoder.rawTakeOverrun());
    auto levels=std::make_unique<bool[]>(captured.size());
    std::vector<uint16_t> durations;
    for(size_t i=0;i<captured.size();++i) {
        levels[i]=captured[i]>0;durations.push_back(captured[i]>0 ? captured[i] : -captured[i]);
    }
    CC1101Driver radio; assert(radio.begin(42,41,2,38,39,40));
    for(uint8_t preset=0;preset<RF_PRESET_COUNT;++preset) {
        assert(saveSubGhzFileRAW(files,"/record.sub",433920000,levels.get(),durations.data(),
                               captured.size(),RfPreset(preset).flipperName));
        SubGhzKeyRecord rec;
        assert(loadSubGhzFile(files,"/record.sub",rec));
        assert(rec.rawCount==captured.size() && rec.frequencyHz==433920000);
        assert(std::equal(captured.begin(),captured.end(),rec.rawData));
        assert(radio.setFrequency(rec.frequencyHz));
        rmtWrites.clear();rmtWriteCalls=0;
        assert(radio.transmitRaw(rec.rawData,rec.rawCount,7,preset));
        assert(rmtWriteCalls==1 && !rmtActive && SPI.marc==1);
        assert(SPI.regs[CC1101_MDMCFG3]==(preset<RF_PRESET_FM238 ? 0x32 : 0x83));
        compare(captured);
    }
    // Imported RAW with a single long gap must not be clipped to 60000us.
    std::vector<int32_t> longGap{33,-66,33,-90000,66,-33,66,-130000};
    assert(saveSubGhzFileRAWSigned(files,"/long.sub",315000000,longGap.data(),longGap.size(),RfPreset(0).flipperName));
    SubGhzKeyRecord rec;assert(loadSubGhzFile(files,"/long.sub",rec));
    assert(rec.rawData[3]==-90000 && rec.rawData[7]==-130000);
    assert(radio.setFrequency(rec.frequencyHz));rmtWrites.clear();
    assert(radio.transmitRaw(rec.rawData,rec.rawCount,7,0));compare(longGap);
    // Recording/file/replay capacity is unified and is no longer a static 4KB record.
    std::vector<int32_t> large(12000);
    for(size_t i=0;i<large.size();++i) large[i]=(i%2) ? -66 : 33;
    assert(saveSubGhzFileRAWSigned(files,"/large.sub",433920000,large.data(),large.size(),RfPreset(0).flipperName));
    assert(loadSubGhzFile(files,"/large.sub",rec));assert(rec.rawCount==large.size());
    rmtWrites.clear();assert(radio.transmitRaw(rec.rawData,rec.rawCount,7,0));compare(large);
    // No TX state / register failure must be reported as successful waveform playback.
    SPI.failTx=true;rmtWriteCalls=0;
    assert(!radio.transmitRaw(rec.rawData,rec.rawCount,7,0));
    assert(rmtWriteCalls==0 && !rmtActive && SPI.marc==1);SPI.failTx=false;
    SPI.dropWriteAddr=CC1101_MDMCFG3;
    assert(!radio.transmitRaw(rec.rawData,rec.rawCount,7,0));
    assert(rmtWriteCalls==0 && !rmtActive);SPI.dropWriteAddr=-1;
    // Flipper-style file playback: >UINT16 and multiple FIFO wraps, a single RMT transaction.
    std::vector<int32_t> continuous(70000);
    for(size_t i=0;i<continuous.size();++i) continuous[i]=(i%2) ? (i%101==0 ? -130000 : -66) : 33;
    {
        FlipperFormat writer;
        assert(writer.openWrite(files,"/continuous.sub"));
        assert(writer.writeHeader("Flipper SubGhz RAW File",1));
        assert(writer.writeKV_U32("Frequency",433920000));
        assert(writer.writeKV("Preset",RfPreset(0).flipperName));
        assert(writer.writeKV("Protocol","RAW"));
        for(size_t offset=0;offset<continuous.size();offset+=256)
            assert(writer.writeKV_I32Array("RAW_Data",continuous.data()+offset,
                                          std::min((size_t)256,continuous.size()-offset)));
    }
    for(uint8_t preset=0;preset<RF_PRESET_COUNT;++preset) {
        auto& content=*testFiles.at("/continuous.sub");
        auto begin=content.find("Preset: ")+8;
        content.replace(begin,content.find('\n',begin)-begin,RfPreset(preset).flipperName);
        RawSubGhzReader file;
        assert(file.open(files,"/continuous.sub"));
        assert(RfPresetIdFromName(file.preset)==preset);
        assert(radio.setFrequency(file.frequencyHz));
        rmtWrites.clear();rmtWriteCalls=0;
        bool streamed=radio.transmitRawStream(RawSubGhzReader::read,&file,7,preset);
        if(!streamed) std::cerr << "stream failed count=" << file.count << " writes=" << rmtWriteCalls << " encoded=" << rmtStreamEncoded << " marc=" << (int)SPI.marc << "\n";
        assert(streamed);
        assert(file.count==continuous.size() && rmtWriteCalls==1 && !rmtActive);
        compare(continuous);
    }
    // Read errors, a lost hardware completion, stop, and a producer underrun all report failure.
    RawSubGhzReader shortFile;
    assert(shortFile.open(files,"/long.sub"));
    rmtNeverComplete=true;rmtWriteCalls=0;
    assert(!radio.transmitRawStream(RawSubGhzReader::read,&shortFile,7,0));
    assert(!rmtActive);rmtNeverComplete=false;
    assert(shortFile.open(files,"/long.sub"));
    rmtPollHook=[](){radioClockUs+=1;}; // Normal callbacks do not interrupt playback.
    assert(radio.transmitRawStream(RawSubGhzReader::read,&shortFile,7,0));rmtPollHook=nullptr;
    assert(shortFile.open(files,"/long.sub"));
    rmtPollHook=[](){ static_cast<RawRmtStream*>(rmtSimpleContext)->markUnderrun(); };
    assert(!radio.transmitRawStream(RawSubGhzReader::read,&shortFile,7,0));
    assert(!rmtActive);rmtPollHook=nullptr;
    assert(shortFile.open(files,"/long.sub"));
    stopTarget=&radio;rmtPollHook=[](){stopTarget->cancelTransmit();};
    assert(!radio.transmitRawStream(RawSubGhzReader::read,&shortFile,7,0));
    assert(!rmtActive);radio.resetTransmitCancel();rmtPollHook=nullptr;
    assert(shortFile.open(files,"/long.sub"));
    SPI.failTx=true;rmtWriteCalls=0;
    assert(!radio.transmitRawStream(RawSubGhzReader::read,&shortFile,7,0));
    assert(!rmtActive && rmtWriteCalls==0);SPI.failTx=false;
    assert(shortFile.open(files,"/long.sub"));
    struct ErrorSource {unsigned n=0;};ErrorSource source;
    auto bad=[](void* context,int32_t& value)->int {
        auto* p=static_cast<ErrorSource*>(context);
        if(++p->n==10000) return -1;
        value=(p->n%2) ? 33 : -66;return 1;
    };
    assert(!radio.transmitRawStream(bad,&source,7,0));assert(!rmtActive);
    radio.cancelTransmit();rmtWriteCalls=0;
    assert(!radio.transmitRawStream(RawSubGhzReader::read,&shortFile,7,0));
    assert(rmtWriteCalls==0);radio.resetTransmitCancel();
    RawRmtStream empty;
    rmt_symbol_word_t symbols[48];bool done=false;
    assert(empty.consume(symbols,48,&done)==0 && done && empty.hasUnderrun());
    // Oversized and malformed files fail explicitly rather than replaying a silent prefix.
    std::string header="Filetype: Flipper SubGhz RAW File\nFrequency: 433920000\nPreset: FuriHalSubGhzPresetOok270Async\nProtocol: RAW\nRAW_Data:";
    auto text=std::make_shared<std::string>(header);
    for(unsigned i=0;i<SUB_RAW_MAX_PULSES+1;++i) *text+=" 33";
    *text+='\n';testFiles["/oversize.sub"]=text;
    assert(!loadSubGhzFile(files,"/oversize.sub",rec) && !rec.rawData && !rec.rawCount);
    for(const char* invalid : {" 33 -66 abc\n"," 33 2147483648\n"," 33 -66x\n"}) {
        testFiles["/invalid.sub"]=std::make_shared<std::string>(header+invalid);
        assert(!loadSubGhzFile(files,"/invalid.sub",rec) && !rec.rawData);
    }
    failRmtAlloc=true;
    assert(!loadSubGhzFile(files,"/large.sub",rec) && !rec.rawData);
    failRmtAlloc=false;failPsramAlloc=true; // Boards without PSRAM use checked internal allocation.
    assert(loadSubGhzFile(files,"/record.sub",rec));failPsramAlloc=false;
    std::cout << "PASS: capture -> multi-line .sub -> reload -> RMT: all transitions/durations match for 33us symbols, >1024 segments, 90/130ms gaps and four presets; 12000-segment replay; 70000-segment streamed replay over FIFO wraps in a single transaction; TX/register/producer/timeout failure; oversize/malformed/OOM rejection\n";
}
