#include "CC1101Driver.h"
#include "FrequencyScanner.h"
#include "RFSubGhzPresets.h"
#include <iostream>
#include "fixtures/rx_418_observed.h"
#include "fixtures/rx_433_observed.h"
#include "fixtures/rx_433_retry.h"
#include "fixtures/rx_433_success.h"

// Compile the exact production receive configuration and antenna functions.
enum RFBand { RF_BAND_LOW, RF_BAND_HIGH };
#include "IOPin.h"
struct { int level=0; int getRxSensitivity() { return level; } } systemSetting;
struct RadioHelper {
    bool IsRawTransmitting() const { return false; }
    CC1101Driver cc1101;
    RFSignalDecoder decoder;
    FrequencyScanner scanner{cc1101,decoder};
    float fixedFreqMHz=433.92f;
    bool beginOokReceive(const float*, int);
    bool beginFixedReceiveProfile(bool wide) {
        int oldLevel = systemSetting.level;
        systemSetting.level = wide ? 1 : 0;
        const bool ok = beginOokReceive(&fixedFreqMHz, 1);
        systemSetting.level = oldLevel;
        return ok;
    }
    bool configureReceive(float, uint8_t, bool);
    void applyRxSensitivity(int);
    void setAntennaBand(RFBand);
};
#include "receive_configuration.inc"

static void edge(uint32_t duration, bool nextHigh, bool resetTime = false) {
    static uint64_t lastEdgeUs = 0;
    if (resetTime || duration == 60000) lastEdgeUs = radioClockUs; // 新信号前的等待
    lastEdgeUs += duration;
    assert(lastEdgeUs >= radioClockUs);
    radioClockUs = lastEdgeUs; // 边沿间隔由发射端决定，不能加上主循环执行时间
    pinLevels[40] = nextHigh;
    assert(pinIsr[40]);
    pinIsr[40]();
}
static void princeton(FrequencyScanner& scanner, uint32_t code) {
    // Guard anchors the frame; GPIO level after edge is the following segment.
    for (int i=23;i>=0;i--) {
        edge((code>>i)&1 ? 900 : 300, false);
        edge((code>>i)&1 ? 300 : 900, true);
    }
    edge(300,false);
    edge(9600,true);scanner.loop();
}
static void doubleFrameState(RFSignalDecoder& decoder) {
    for (bool inverted : {false,true}) {
        decoder.reset();
        uint16_t pulses[50]; bool levels[50];
        const uint32_t code=0xa5b6c7;
        for(int i=0;i<24;i++) {
            bool bit=(code>>(23-i))&1;
            pulses[2*i]=bit ? 960 : 320; pulses[2*i+1]=bit ? 320 : 960;
            levels[2*i]=!inverted; levels[2*i+1]=inverted;
        }
        pulses[48]=320;pulses[49]=9600;levels[48]=!inverted;levels[49]=inverted;
        RFSignal signal{};
        const uint16_t guard=9600; const bool guardLevel=inverted;
        assert(!decoder.decodeFlipperStyle(&guard,&guardLevel,1,signal));
        assert(!decoder.decodeFlipperStyle(pulses,levels,50,signal));
        assert(decoder.decodeFlipperStyle(pulses,levels,50,signal));
        assert(signal.bitCount==24 && signal.data[0]==0xa5 && signal.data[1]==0xb6 && signal.data[2]==0xc7);
        // 23 位截断帧不能借前一包的 state 凑成完整码。
        decoder.reset();
        pulses[46]=320;pulses[47]=9600;
        assert(!decoder.decodeFlipperStyle(pulses,levels,48,signal));
        assert(!decoder.decodeFlipperStyle(pulses,levels,48,signal));
    }
    decoder.reset();
    uint16_t pulses[128]; bool levels[128];
    auto n=CameStyleProtocolDecoder::buildFrame(&PROTO_CAME,0xa5b,12,320,pulses,levels);
    RFSignal signal{};
    assert(n>0);
    assert(!decoder.decodeFlipperStyle(pulses,levels,n,signal));
    // CAME 编码帧以 Guard 开始；第二帧的尾部由第三帧开头的 Guard 闭合。
    assert(!decoder.decodeFlipperStyle(pulses,levels,n,signal));
    assert(decoder.decodeFlipperStyle(pulses,levels,n,signal));
    assert(signal.bitCount==12 && signal.data[0]==0xa5 && signal.data[1]==0xb0);
    // A bad frame breaks the repetition chain; exact length is checked before data.
    decoder.reset();
    uint16_t pwm[51];bool pwmLevels[51];
    pwm[0]=9600;pwmLevels[0]=false;
    for(int i=0;i<24;i++) {
        bool bit=(0xa5b6c7>>(23-i))&1;
        pwm[1+2*i]=bit?900:300;pwm[2+2*i]=bit?300:900;
        pwmLevels[1+2*i]=true;pwmLevels[2+2*i]=false;
    }
    pwm[49]=300;pwm[50]=9600;pwmLevels[49]=true;pwmLevels[50]=false;
    assert(!decoder.decodeFlipperStyle(pwm,pwmLevels,51,signal));
    uint16_t tail[2]={300,9600};bool tailLevels[2]={true,false};
    assert(!decoder.decodeFlipperStyle(pwm+1,pwmLevels+1,46,signal));
    assert(!decoder.decodeFlipperStyle(tail,tailLevels,2,signal));
    assert(!decoder.decodeFlipperStyle(pwm+1,pwmLevels+1,50,signal));
    assert(decoder.decodeFlipperStyle(pwm+1,pwmLevels+1,50,signal));
    CameStyleProtocolDecoder came(&PROTO_CAME);
    auto first=CameStyleProtocolDecoder::buildFrame(&PROTO_CAME,0x5b,12,320,pulses,levels);
    for(int i=0;i<first;i++) assert(!came.feed(levels[i],pulses[i]));
    auto second=CameStyleProtocolDecoder::buildFrame(&PROTO_CAME,0x5b,24,320,pulses,levels);
    for(int i=0;i<second;i++) assert(!came.feed(levels[i],pulses[i]));
    assert(!came.feed(false,24320)); // same numeric code, different bit lengths
    decoder.reset();
}
// End-to-end checks intentionally exercise ISR -> decoder -> scanner confirmation.
static void pwmPacket(FrequencyScanner& scanner, uint64_t code, uint8_t bits,
                      bool inverted=false, uint16_t shortUs=300, uint16_t longUs=900,
                      uint32_t guardUs=9600, bool pollEachEdge=true, bool glitch=false) {
    for (int i=bits-1;i>=0;i--) {
        uint16_t hi=(code>>i)&1 ? longUs : shortUs;
        uint16_t lo=(code>>i)&1 ? shortUs : longUs;
        if(glitch) {
            edge(100,inverted); if(pollEachEdge && !scanner.hasConfirmedSignal()) scanner.loop();
            edge(20,!inverted); if(pollEachEdge && !scanner.hasConfirmedSignal()) scanner.loop();
            edge(hi-120,inverted);
        } else edge(hi,inverted);
        if(pollEachEdge && !scanner.hasConfirmedSignal()) scanner.loop();
        edge(lo,!inverted);if(pollEachEdge && !scanner.hasConfirmedSignal()) scanner.loop();
    }
    edge(shortUs,inverted);if(pollEachEdge && !scanner.hasConfirmedSignal()) scanner.loop();
    edge(guardUs,!inverted);
    if(pollEachEdge && !scanner.hasConfirmedSignal()) {
        radioClockUs += 30; // 反向电平已稳定达到滤波门限，消费侧可提交尾 Guard。
        scanner.loop();
    }
}
static void frameConfirmationRegression(RadioHelper& h) {
    FrequencyScanner scanner(h.cc1101,h.decoder);
    float freq=418.0f;
    SignalFrame frame{};
    for (bool scan : {false,true}) {
        float frequencies[]={freq,433.92f};
        auto restart=[&] {
            modelDelayTime=true;
            assert(scanner.begin(frequencies,scan?2:1));
            modelDelayTime=false;
            SPI.rssi=80; // -34dBm, locks range receive to the initial frequency.
            edge(60000,true);scanner.loop();
        };
        // A single 12-bit fragment previously became INTERTECHNO after signal end.
        restart();
        pwmPacket(scanner,0x0b8,12,false,275,772,20983);
        delay(120);scanner.loop();
        assert(!scanner.getConfirmedSignal(frame));
        assert(scanner.isRunning());scanner.stop();
        // Repeated truncated 23-bit PWM packets cannot qualify by length or votes.
        restart();
        for(int i=0;i<5;i++) pwmPacket(scanner,0x492492,23);
        delay(120);scanner.loop();assert(!scanner.getConfirmedSignal(frame));
        scanner.stop();
        // Unsynchronised 2:1 pulse trains cannot become a 23-bit BFT frame.
        restart();
        for(int i=0;i<250;i++) {
            edge(i%3==0?606:304,i%2==0);scanner.loop();
        }
        delay(120);scanner.loop();assert(!scanner.getConfirmedSignal(frame));
        scanner.stop();
        // One real Guard, followed by one full frame, is still only one occurrence.
        restart();edge(9600,true);scanner.loop();
        pwmPacket(scanner,0xa5b6c7,24);
        assert(!scanner.getConfirmedSignal(frame));
        pwmPacket(scanner,0xa5b6c7,24);
        assert(scanner.getConfirmedSignal(frame));
        assert(frame.bitCount==24 && frame.repeatCount>=2 && frame.protocolValidated);
        assert(frame.data[0]==0xa5 && frame.data[1]==0xb6 && frame.data[2]==0xc7);
        scanner.stop();
        // Missing first bit, noise, polarity and glitch filtering must not stop RX early.
        for (bool inverted : {false,true}) {
            restart();
            edge(60000,!inverted);
            pwmPacket(scanner,0xa5b6c7,23,inverted);
            assert(!scanner.getConfirmedSignal(frame));
            for(int i=0;i<3;i++) pwmPacket(scanner,0xa5b6c7,24,inverted,275,825,9900,true,true);
            assert(scanner.getConfirmedSignal(frame));
            assert(frame.bitCount==24 && frame.repeatCount>=2 && frame.protocolValidated);
            assert(frame.data[0]==0xa5 && frame.data[1]==0xb6 && frame.data[2]==0xc7);
            assert(strcmp(frame.protocolName,"PT2262")==0 || strcmp(frame.protocolName,"EV1527")==0);
            scanner.stop();
        }
        // Widths near the user's earlier 304/606us log: framed 2:1 PWM is not
        // Princeton's fixed 3:1 model. It must decode as PWM24 with real timings.
        for (bool inverted : {false,true}) {
            restart();edge(60000,!inverted);
            for(int i=0;i<3 && !scanner.hasConfirmedSignal();i++)
                pwmPacket(scanner,0xa5b6c7,24,inverted,304,606,20983,true,true);
            assert(scanner.getConfirmedSignal(frame));
            assert(frame.bitCount==24 && frame.repeatCount>=2 && strcmp(frame.protocolName,"PWM24")==0);
            assert(frame.data[0]==0xa5 && frame.data[1]==0xb6 && frame.data[2]==0xc7);
            assert(frame.shortPulse==304 && frame.longPulse==606);
            scanner.stop();
        }
        // Preserve the full waveform length; unknown protocol payload length is not
        // inferred by stripping a prefix. This synthetic frame has 39/40/64 pairs.
        for (uint8_t bits : {39,40,64}) {
            for (bool inverted : {false,true}) {
                restart();edge(60000,!inverted);
                const uint64_t code = bits==64 ? 0x9b066c6170abcdefULL : 0x4d833630b8ULL;
                for(int i=0;i<4 && !scanner.hasConfirmedSignal();i++)
                    pwmPacket(scanner,code,bits,inverted,300,600,90000);
                assert(scanner.getConfirmedSignal(frame) && frame.bitCount==bits);
                uint64_t actual=0;
                for(int bit=0;bit<frame.bitCount;bit++)
                    actual=(actual<<1)|((frame.data[bit/8]>>(7-bit%8))&1);
                assert(actual==code && frame.guardDurationUs==90000);
                scanner.stop();
            }
        }
        // A real long inter-frame LOW is a Guard, not a lost-stream marker.
        restart();
        for(int i=0;i<5 && !scanner.hasConfirmedSignal();i++)
            pwmPacket(scanner,0xa5b6c7,24,false,300,600,90000);
        assert(scanner.getConfirmedSignal(frame));
        assert(frame.bitCount==24 && frame.data[0]==0xa5 && frame.data[1]==0xb6 && frame.data[2]==0xc7);
        assert(frame.guardDurationUs==90000 && frame.syncPulse==65535);
        scanner.stop();
        // A 20us glitch in a real long Guard must be glued before protocol feed.
        restart();
        for(int r=0;r<5 && !scanner.hasConfirmedSignal();r++) {
            for(int i=23;i>=0;i--) {
                edge((0xa5b6c7>>i)&1 ? 900:300,false);scanner.loop();
                edge((0xa5b6c7>>i)&1 ? 300:900,true);scanner.loop();
            }
            edge(300,false);scanner.loop();
            edge(6000,true);scanner.loop();
            edge(20,false);scanner.loop();
            edge(3600,true);scanner.loop();
        }
        assert(scanner.getConfirmedSignal(frame));
        assert(frame.bitCount==24 && frame.data[0]==0xa5 && frame.data[1]==0xb6 && frame.data[2]==0xc7);
        scanner.stop();
        // Pause the consumer for three whole frames: no ready-flag edge loss.
        restart();
        for(int i=0;i<3;i++) pwmPacket(scanner,0xa5b6c7,24,false,300,900,9600,false);
        radioClockUs += 100;scanner.loop();
        assert(scanner.getConfirmedSignal(frame) && frame.bitCount==24);
        assert(frame.data[0]==0xa5 && frame.data[1]==0xb6 && frame.data[2]==0xc7);
        scanner.stop();
        // Overflow is a stream discontinuity, never a source of fabricated frames.
        restart();
        for(int i=0;i<RAW_RING_SIZE+100;i++) edge(100,i%2==0);
        auto overruns=h.decoder.receiveOverruns();scanner.loop();
        assert(h.decoder.receiveOverruns()==overruns+1);
        assert(!scanner.getConfirmedSignal(frame));
        edge(60000,true);scanner.loop(); // 长时间断流/换频后，从新突发重新锚定边沿时钟
        for(int i=0;i<4;i++) pwmPacket(scanner,0xa5b6c7,24);
        assert(scanner.getConfirmedSignal(frame) && frame.bitCount==24);
        scanner.stop();
        // Genuine 12-bit CAME still passes its own framing and repetition rules.
        restart();
        uint16_t pulses[128];bool levels[128];
        auto n=CameStyleProtocolDecoder::buildFrame(&PROTO_CAME,0xa5b,12,320,pulses,levels);
        for(int r=0;r<4 && !scanner.hasConfirmedSignal();r++) for(int i=0;i<n;i++) {
            edge(pulses[i],!levels[i]);
            if (!scanner.hasConfirmedSignal()) scanner.loop();
        }
        assert(scanner.getConfirmedSignal(frame));
        assert(frame.bitCount==12 && frame.repeatCount>=2 && strcmp(frame.protocolName,"CAME")==0);
        scanner.stop();
        // CAME uses low/high data pairs and 2:1 timing; 24 bits must stay 24 bits.
        restart();
        n=CameStyleProtocolDecoder::buildFrame(&PROTO_CAME,0xa5b6c7,24,320,pulses,levels);
        for(int r=0;r<4 && !scanner.hasConfirmedSignal();r++) for(int i=0;i<n;i++) {
            edge(pulses[i],!levels[i]);
            if (!scanner.hasConfirmedSignal()) scanner.loop();
        }
        assert(scanner.getConfirmedSignal(frame));
        assert(frame.bitCount==24 && strcmp(frame.protocolName,"CAME")==0);
        assert(frame.data[0]==0xa5 && frame.data[1]==0xb6 && frame.data[2]==0xc7);
        scanner.stop();
    }
    RFSignal guessed{};
    guessed.bitCount=12;guessed.shortPulse=275;guessed.longPulse=772;
    guessed.syncPulse=20983;guessed.encodingType=ENCODING_PWM;
    assert(!h.decoder.identifyProtocol(guessed));
    guessed.bitCount=23;guessed.shortPulse=304;guessed.longPulse=606;
    guessed.syncPulse=0;guessed.encodingType=ENCODING_MANCHESTER;
    assert(!h.decoder.identifyProtocol(guessed));
    modelDelayTime=true;
    std::cout << "PASS: full 32-bit 90ms Guard, 24/39/40/64-pair repeat verification, observed jitter, guard-glitch recovery, narrow/wide tuning; fixed/range ISR confirmation rejects single 12-bit fragments, repeated truncated 23-bit packets and unsynchronised 2:1 noise; accepts exact 24-bit data, both polarities, glitches, delayed consumer, overrun recovery and real 12-bit CAME\n";
}
// The log windows are not complete frames. Replay the post-gap runs under an
// explicit tail-Guard hypothesis; these tests prove compatibility, not vendor framing.
static void observedPulseCompatibility(RFSignalDecoder& decoder) {
    assert(sizeof(RawSeg)==4); // Full normal durations reuse padding, no doubled ring RAM.
    for(bool inverted : {false,true}) {
        decoder.reset();
        uint32_t guard=60000;bool guardLevel=inverted;RFSignal signal{};
        assert(!decoder.decodeFlipperStyle(&guard,&guardLevel,1,signal));
        for(int snapshot=0;snapshot<2;snapshot++) {
            const int32_t* signedPulses=snapshot==0?observed418_1:observed418_2;
            uint32_t pulses[80];bool levels[80];
            // Both excerpts have the software gap marker at index48, then79 segments.
            for(int i=0;i<79;i++) {
                int32_t v=signedPulses[49+i];
                pulses[i]=v>0?v:-v;levels[i]=(v>0)!=inverted;
            }
            pulses[79]=60000;levels[79]=inverted; // assumed next Guard; not present in log
            bool decoded=decoder.decodeFlipperStyle(pulses,levels,80,signal);
            assert(decoded==(snapshot==1));
        }
        assert(signal.bitCount==39 && strcmp(signal.protocolName,"PWM39")==0);
        uint64_t actual=0;
        for(int bit=0;bit<signal.bitCount;bit++)
            actual=(actual<<1)|((signal.data[bit/8]>>(7-bit%8))&1);
        assert(actual==0x4d833630b8ULL && signal.guardDurationUs==60000);
    }
    decoder.reset();
    std::cout << "PASS: captured 418MHz pulse runs with observed jitter, under explicit trailing-Guard hypothesis; no 24-bit payload assumed\n";
}
static void observedSuccessLatency(RadioHelper& h) {
    systemSetting.level=1;h.fixedFreqMHz=433.92f;
    modelDelayTime=true;assert(h.beginOokReceive(&h.fixedFreqMHz, 1));modelDelayTime=false;
    assert(SPI.regs[CC1101_MDMCFG4]==0x17);
    edge(60000,false);
    uint64_t started=0;
    for(int frame=0;frame<2;frame++) {
        const int32_t* data=frame?success433_2:success433_1;
        // The closing Guard of frame1 is the opening Guard of frame2.
        for(int i=frame?1:0;i<51;i++) {
            edge(abs(data[i]),data[i]<0);h.scanner.loop();
            if(frame==0 && i==0) started=radioClockUs;
        }
        radioClockUs+=30;h.scanner.loop();
        assert(h.scanner.hasConfirmedSignal()==(frame==1));
    }
    auto elapsed=radioClockUs-started;
    SignalFrame result{};assert(h.scanner.getConfirmedSignal(result));
    assert(result.bitCount==24 && result.data[0]==0x97 && result.data[1]==0xb6 && result.data[2]==0x28);
    assert(strcmp(result.protocolName,"PT2262")==0 && result.repeatCount==2);
    assert(result.shortPulse==317 && result.longPulse==951 && result.guardDurationUs==8790);
    assert(elapsed<100000); // Actual two-frame waveform is79157us, not2.5seconds.
    h.scanner.stop();
    std::cout << "PASS: actual 433.92MHz two frames -> 97B628 in " << elapsed << "us\n";
}

static void sharedReceiveSetup(RadioHelper& h) {
    const float frequencies[] = {310, 315, 330, 350, 390, 418, 430.5, 431.5, 432.8, 433.92};
    modelDelayTime = true;
    for (int sensitivity : {0, 1, 2}) {
        systemSetting.level = sensitivity;
        for (float frequency : frequencies) {
            assert(h.beginOokReceive(&frequency, 1));
            assert(SPI.regs[CC1101_MDMCFG4] == (sensitivity == 0 ? 0x67 : 0x17));
            assert(SPI.regs[CC1101_TEST2] == (sensitivity == 2 ? 0x88 : 0x81));
            const int tunes = SPI.tuneCount;
            delay(3000);
            h.scanner.loop();
            assert(h.scanner.isRunning() && SPI.tuneCount == tunes);
            h.scanner.stop();
        }
    }

    systemSetting.level = 1;
    SPI.rssi = 184;  // Weak RSSI. The scan can still lock on complete frames.
    for (int first : {0, 4}) {
        const int count = first == 0 ? 4 : 6;
        assert(h.beginOokReceive(frequencies + first, count));
        delay(FAST_SCAN_DWELL_MS + 1);
        h.scanner.loop();
        assert(h.scanner.getCurrentFreqIndex() == 1);
        modelDelayTime = false;
        // Waveform starts within this visit; avoid advancing the virtual clock
        // through a 60ms idle without calling scanner.loop().
        edge(1, false, true);
        edge(9600, true);
        h.scanner.loop();
        for (int i = 0; i < 4 && !h.scanner.hasConfirmedSignal(); ++i)
            pwmPacket(h.scanner, 0x97b628, 24, false, 320, 960, 9600);
        SignalFrame frame{};
        assert(h.scanner.getConfirmedSignal(frame) && frame.freqIndex == 1 && frame.bitCount == 24);
        h.scanner.stop();
        modelDelayTime = true;
    }
    std::cout << "PASS: all 10 fixed frequencies and both scan lists share OOK setup and report tuned frequency\n";
}
static void adaptiveScanTiming(RadioHelper& h) {
    const float frequencies[] = {390, 418, 430.5, 431.5, 432.8, 433.92};
    modelDelayTime = true;

    for (int count : {4, 6}) {
        SPI.rssi = 184; // -110dBm.
        assert(h.scanner.begin(frequencies, count));
        const uint64_t start = radioClockUs;
        int next = 1;
        while (next <= count) {
            delay(5);
            h.scanner.loop();
            if (h.scanner.getCurrentFreqIndex() == next % count) ++next;
            assert(radioClockUs - start < uint64_t(count) * 45000);
        }
        std::cout << "PASS: idle sweep of " << count << " channels in "
                  << (radioClockUs - start) / 1000 << "ms\n";
        h.scanner.stop();
    }

    SPI.rssi = 216; // -94dBm, weak activity, below the normal lock threshold.
    assert(h.scanner.begin(frequencies, 6));
    h.scanner.loop(); delay(5); h.scanner.loop();
    SPI.rssi = 184;
    delay(FAST_SCAN_DWELL_MS); h.scanner.loop();
    assert(h.scanner.getCurrentFreqIndex() == 0);
    delay(160); h.scanner.loop();
    assert(h.scanner.getCurrentFreqIndex() == 1);
    h.scanner.stop();

    SPI.rssi = 80; // A strong burst disappears without a frame.
    assert(h.scanner.begin(frequencies, 6));
    h.scanner.loop();
    SPI.rssi = 184;
    delay(5); h.scanner.loop();
    delay(105); h.scanner.loop();
    assert(h.scanner.getCurrentFreqIndex() == 0);
    delay(10); h.scanner.loop();
    assert(h.scanner.getCurrentFreqIndex() == 1);
    h.scanner.stop();

    SPI.rssi = 80; // A strong interferer cannot pin the scanner indefinitely.
    assert(h.scanner.begin(frequencies, 6));
    h.scanner.loop(); delay(200); h.scanner.loop();
    assert(h.scanner.getCurrentFreqIndex() == 0);
    delay(45); h.scanner.loop();
    assert(h.scanner.getCurrentFreqIndex() == 1);
    h.scanner.stop();
    const float longGuardChannels[] = {418.0f, 433.92f};
    SPI.rssi = 80;
    assert(h.scanner.begin(longGuardChannels, 2));
    h.scanner.loop();  // Strong carrier locks the first frequency.
    SPI.rssi = 184;  // Subsequent RSSI polls can all land in OOK low periods.
    modelDelayTime = false;
    edge(1, false, true);
    edge(56002, true);
    h.scanner.loop();
    for (int i = 0; i < 4 && !h.scanner.hasConfirmedSignal(); ++i)
        pwmPacket(h.scanner, 0x4d833630b8ULL, 39, false, 304, 609, 56002);
    SignalFrame frame{};
    assert(h.scanner.getConfirmedSignal(frame) && frame.freqIndex == 0 && frame.bitCount == 39);
    h.scanner.stop();
    modelDelayTime = true;
    std::cout << "PASS: weak activity extends listening, quiet lock releases, sustained interference times out\n";
}
int main() {
    RadioHelper h;
    assert(h.cc1101.begin(42,41,2,38,39,40));
    assert(pinModes[39]==INPUT && pinModes[40]==INPUT);
    assert(SPI.pa[0]==0 && SPI.pa[1]==CC1101Driver::txPowerToPaByte(7));
    h.cc1101.writeReg(CC1101_MDMCFG2,0x37);
    h.cc1101.disableSyncWord();
    assert(h.cc1101.readReg(CC1101_MDMCFG2)==0x30);
    // Reset works repeatedly while SCK/MOSI are already owned by hardware SPI.
    for(int i=0;i<3;i++) assert(h.cc1101.reset() && h.cc1101.checkConnection());
    assert(spiPins[42] && spiPins[41] && spiPins[2] && !spiPins[38]);
    h.setAntennaBand(RF_BAND_LOW);assert(pinLevels[47]==LOW);
    h.setAntennaBand(RF_BAND_HIGH);assert(pinLevels[47]==HIGH);
    // Return to OOK after each RAW preset and a dirty TX register/pin state.
    for(uint8_t id=0;id<RF_PRESET_COUNT;id++) {
        assert(h.configureReceive(315.0f,id,false));
        assert(SPI.regs[CC1101_IOCFG2]==0x0d && SPI.regs[CC1101_PKTCTRL0]==0x32);
        assert(h.cc1101.setRxMode());
        h.cc1101.writeReg(CC1101_IOCFG2,0x0b);
        h.cc1101.writeReg(CC1101_PKTCTRL0,0x05);
        h.cc1101.writeReg(CC1101_CHANNR,9);
        pinModes[39]=OUTPUT;
        assert(h.configureReceive(433.92f,RF_PRESET_AM270,true));
        assert(SPI.regs[CC1101_MDMCFG2]==0x30 && SPI.regs[CC1101_MDMCFG3]==0x32);
        assert(SPI.regs[CC1101_FREND1]==0xb6 && SPI.regs[CC1101_CHANNR]==0);
        assert(pinModes[39]==INPUT && pinModes[40]==INPUT && pinIsr[40]);
    }
    for (float freq : {418.0f, 433.92f}) {
        assert(h.configureReceive(freq, RF_PRESET_AM650, false));
        assert(SPI.regs[CC1101_IOCFG0] == 0x0D);
        assert(SPI.regs[CC1101_MDMCFG4] == 0x17 && SPI.regs[CC1101_MDMCFG3] == 0x32);
        assert(SPI.regs[CC1101_AGCCTRL2] == 0x07 && SPI.regs[CC1101_AGCCTRL0] == 0x91);
        assert(SPI.regs[CC1101_TEST2] == 0x81 && SPI.regs[CC1101_TEST1] == 0x35);
    }
    // Wide sensitivity must apply all dependent tuning, including after narrow RX/TX.
    for(int level : {2,0,2,1,0}) {
        systemSetting.level=level;
        assert(h.configureReceive(418.0f,RF_PRESET_AM270,true));
        assert(SPI.regs[CC1101_MDMCFG4]==(level==2?0x17:0x67));
        assert(SPI.regs[CC1101_AGCCTRL0]==(level==2?0x91:0x40));
        assert(SPI.regs[CC1101_FIFOTHR]==(level==2?0x07:0x47));
        assert(SPI.regs[CC1101_TEST2]==(level==2?0x88:0x81));
        assert(SPI.regs[CC1101_TEST1]==(level==2?0x31:0x35));
    }
    SPI.dropWriteAddr=CC1101_TEST1;
    assert(!h.configureReceive(418.0f,RF_PRESET_AM270,true));
    SPI.dropWriteAddr=-1;
    // Decode the user's known24-bit value through both explicit profiles.
    // Preferred initial profile and successful reuse are tested with real frames below.
    systemSetting.level=2;
    for(bool wide : {false,true}) {
        modelDelayTime=true;assert(h.beginFixedReceiveProfile(wide));modelDelayTime=false;
        assert(SPI.regs[CC1101_MDMCFG4]==(wide?0x17:0x67));
        assert(SPI.regs[CC1101_AGCCTRL2]==(wide?0x07:0x03));
        assert(SPI.regs[CC1101_AGCCTRL0]==(wide?0x91:0x40));
        assert(SPI.regs[CC1101_FIFOTHR]==(wide?0x07:0x47));
        edge(60000,true);
        for(int i=0;i<4 && !h.scanner.hasConfirmedSignal();i++)
            pwmPacket(h.scanner,0x97b628,24,false,320,960,9600);
        SignalFrame known{};assert(h.scanner.getConfirmedSignal(known));
        assert(known.bitCount==24 && known.data[0]==0x97 && known.data[1]==0xb6 && known.data[2]==0x28);
        h.scanner.stop();
    }
    // Fast100/300us24-bit remotes need a3.2ms Guard and their jittered98us
    // pulses must not be rejected by a100us parser limit above the80us filter.
    modelDelayTime=true;assert(h.beginFixedReceiveProfile(false));modelDelayTime=false;
    edge(60000,true);
    for(int i=0;i<4 && !h.scanner.hasConfirmedSignal();i++)
        pwmPacket(h.scanner,0x97b628,24,false,98,294,3200);
    SignalFrame fast{};assert(h.scanner.getConfirmedSignal(fast));
    assert(fast.bitCount==24 && fast.data[0]==0x97 && fast.data[1]==0xb6 && fast.data[2]==0x28);
    h.scanner.stop();
    observedSuccessLatency(h);
    // Flipper's30us filter preserves the valid33/66us pulses which the previous
    //80us filter merged into adjacent levels. Known payload remains a test only.
    for(uint16_t shortUs : {33,66}) {
        modelDelayTime=true;assert(h.beginFixedReceiveProfile(false));modelDelayTime=false;
        edge(60000,true);
        for(int i=0;i<4 && !h.scanner.hasConfirmedSignal();i++)
            pwmPacket(h.scanner,0x97b628,24,false,shortUs,shortUs*3,shortUs*31);
        SignalFrame tiny{};assert(h.scanner.getConfirmedSignal(tiny));
        assert(tiny.bitCount==24 && tiny.data[0]==0x97 && tiny.data[1]==0xb6 && tiny.data[2]==0x28);
        RawSeg raw[RX_TRACE_SIZE];uint32_t total,glitches;
        auto n=h.decoder.edgeSnapshot(raw,RX_TRACE_SIZE,total,glitches);
        bool sawShort=false;
        for(int i=0;i<n;i++) if(raw[i].durUs==shortUs && !raw[i].durationHigh) sawShort=true;
        assert(sawShort && total>=100 && glitches==0);
        h.scanner.stop();
    }
    // TI async output permits one sample of jitter (~33us at3.79kBaud).
    // Test a100/300us packet whose short pulses vary66..133us, including
    // the early samples; uniform synthetic timing alone misses this failure.
    modelDelayTime=true;assert(h.beginFixedReceiveProfile(false));modelDelayTime=false;
    edge(60000,true);
    const uint16_t sampledShort[]={100,66,133,100}, sampledLong[]={300,333,267,300};
    for(int repeat=0;repeat<4 && !h.scanner.hasConfirmedSignal();repeat++) {
        for(int bit=23;bit>=0;bit--) {
            bool value=(0x97b628>>bit)&1;uint16_t sp=sampledShort[bit%4],lp=sampledLong[bit%4];
            edge(value?lp:sp,false);h.scanner.loop();edge(value?sp:lp,true);h.scanner.loop();
        }
        edge(100,false);h.scanner.loop();edge(3100,true);radioClockUs+=30;h.scanner.loop();
    }
    SignalFrame sampled{};assert(h.scanner.getConfirmedSignal(sampled));
    assert(sampled.bitCount==24 && sampled.data[0]==0x97 && sampled.data[1]==0xb6 && sampled.data[2]==0x28);
    assert(sampled.shortPulse==99 && sampled.longPulse==300);
    h.scanner.stop();
    std::cout << "PASS:100/300us24-bit0x97B628 with66..133us short-pulse sample jitter\n";
    sharedReceiveSetup(h);
    adaptiveScanTiming(h);
    modelDelayTime=true;systemSetting.level=0;
    frameConfirmationRegression(h);
    // The 433MHz excerpts contain no verified0x97B628 frame. Repeating this
    // noise-like window must never manufacture the requested value.
    h.decoder.reset();
    uint32_t noisyDur[128];bool noisyLv[128];RFSignal noisySignal{};
    for(int i=0;i<128;i++) { noisyDur[i]=abs(observed433_1[i]);noisyLv[i]=observed433_1[i]>0; }
    for(int i=0;i<3;i++) assert(!h.decoder.decodeFlipperStyle(noisyDur,noisyLv,128,noisySignal));
    h.decoder.reset();
    std::cout << "PASS: exact24-bit0x97B628 with both fixed RX profiles, including fast98/294us data and3.2ms Guard; 433MHz supplied noise-like excerpt is rejected\n";
    for(bool second : {false,true}) {
        h.decoder.reset();
        uint32_t dur[112];bool lv[112];RFSignal result{};
        const int32_t* data=second?retry433_2:retry433_1;
        uint16_t n=second?112:94;
        for(int i=0;i<n;i++) {dur[i]=abs(data[i]);lv[i]=data[i]>0;}
        // Independent, overlapping snapshots cannot be spliced into a new frame.
        for(int repeat=0;repeat<3;repeat++) assert(!h.decoder.decodeFlipperStyle(dur,lv,n,result));
    }
    std::cout << "PASS: latest433MHz94/112-segment filtered windows remain unconfirmed\n";
    observedPulseCompatibility(h.decoder);
    doubleFrameState(h.decoder);
    // 用真实 ISR 验证 >15ms 的 CAME Guard 仍送到协议机，而非被包间隔逻辑丢弃。
    modelDelayTime=false;
    uint16_t cameDur[128];bool cameLevels[128];
    auto cameN=CameStyleProtocolDecoder::buildFrame(&PROTO_CAME,0xa5b,12,320,cameDur,cameLevels);
    edge(60000,false);
    bool cameReceived=false;
    for(int repeat=0;repeat<4;repeat++) {
        for(int i=0;i<cameN;i++) {
            edge(cameDur[i],!cameLevels[i]);
            if(i==0) {
                RFSignal s{};
                if(h.decoder.available(s) && s.bitCount==12 && strcmp(s.protocolName,"CAME")==0)
                    cameReceived=s.data[0]==0xa5 && s.data[1]==0xb0;
            }
        }
    }
    assert(cameReceived);
    modelDelayTime=true;h.decoder.reset();
    SPI.dropWriteAddr=CC1101_IOCFG2;
    assert(!h.configureReceive(433.92f,RF_PRESET_AM270,true));
    SPI.dropWriteAddr=CC1101_FREQ0;
    assert(!h.configureReceive(433.92f,RF_PRESET_AM270,true));
    SPI.dropWriteAddr=-1;
    assert(h.configureReceive(433.92f,RF_PRESET_AM270,true));
    FrequencyScanner scanner(h.cc1101,h.decoder);
    float freq=433.92f;
    SPI.rssi=184; // Below scanning threshold; fixed receive must still decode.
    assert(scanner.begin(&freq,1));
    int tunes=SPI.tuneCount;
    for(int i=0;i<400;i++) { delay(5);scanner.loop(); }
    assert(SPI.tuneCount==tunes); // 2 seconds listening without retuning/reset.
    modelDelayTime=false; // 波形阶段由边沿驱动时间；同步模型不模拟主循环与 ISR 并行
    edge(60000,true);
    for(int i=0;i<4 && !scanner.hasConfirmedSignal();i++) princeton(scanner,0xa5b6c7);
    SignalFrame frame{};
    assert(scanner.getConfirmedSignal(frame));
    assert(frame.bitCount==24 && frame.data[0]==0xa5 && frame.data[1]==0xb6 && frame.data[2]==0xc7);
    assert(SPI.tuneCount==tunes && h.decoder.edgeCount()>190);
    scanner.stop();
    modelDelayTime=true;
    float table[]={433.92f,315.0f};
    assert(scanner.begin(table,2));
    tunes=SPI.tuneCount;
    modelDelayTime=false;
    edge(60000,true);
    edge(900,false); // Capture begins before the scanner sees RSSI.
    edge(300,true);
    SPI.rssi=static_cast<uint8_t>((-85+74)*2);
    scanner.loop();
    assert(SPI.tuneCount==tunes && scanner.getCurrentFreqIndex()==0);
    for(int i=0;i<4 && !scanner.hasConfirmedSignal();i++) princeton(scanner,0xa5b6c7);
    assert(scanner.getConfirmedSignal(frame) && frame.bitCount==24 && frame.freqIndex==0);
    scanner.stop();
    modelDelayTime=true;
    // Silent / weak frequencies continue scanning instead of locking forever.
    SPI.rssi=184;assert(scanner.begin(table,2));tunes=SPI.tuneCount;
    delay(FAST_SCAN_DWELL_MS + 1);scanner.loop();assert(scanner.getCurrentFreqIndex()==1 && SPI.tuneCount==tunes+1);
    scanner.stop();
    SPI.failRx=true;assert(!scanner.begin(&freq,1));assert(!scanner.isRunning());
    SPI.failRx=false;
    float oneBad[]={350.0f,315.0f};
    SPI.failRxFrequencyWord=(uint64_t(350000000)<<16)/26000000;
    assert(scanner.begin(oneBad,2) && scanner.getCurrentFreqIndex()==1);
    delay(FAST_SCAN_DWELL_MS + 1);scanner.loop();assert(scanner.isRunning() && scanner.getCurrentFreqIndex()==1);
    scanner.stop();SPI.failRxFrequencyWord=0;
    // No SPI clocks if SO/CHIP_RDYn never drops; close every transaction on timeout.
    SPI.stuckReady=true;int transfers=SPI.transfers;
    assert(h.cc1101.readReg(CC1101_IOCFG2)==0xff);
    assert(SPI.transfers==transfers && !SPI.transaction && !SPI.selected);
    SPI.stuckReady=false;
    std::cout << "PASS: SPI GPIO ownership/reset/ready timeout, sync bits, antenna polarity, all RAW presets -> OOK RX, PWM/CAME double-frame state, both polarities, 23-bit rejection, fixed weak-RSSI frame, immediate scan lock, RX failure\n";
}
