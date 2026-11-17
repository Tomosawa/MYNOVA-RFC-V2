#include "CC1101Driver.h"
#include <esp_heap_caps.h>
#include <iostream>

static CC1101Driver radio;
static void resetRmt() {
    assert(!rmtActive && !rmtPending);
    rmtNeverComplete = rmtInitFails = failRmtAlloc = false;
    rmtRejectWrite = rmtWriteCalls = rmtDeinitCalls = 0;
    rmtPollHook = nullptr;
    rmtWrites.clear();
    radio.resetTransmitCancel();
}
static void cleaned() {
    assert(!rmtActive && !rmtPending && SPI.marc == 1 && pinModes[39] == INPUT);
    assert(rmtDeinitCalls == 1);
}
static uint64_t waveformDuration(const std::vector<rmt_data_t>& items) {
    uint64_t result = 0;
    for (auto item : items) result += item.duration0 + item.duration1;
    return result;
}
static void checkStoredCode(const uint8_t* data, uint16_t bits,
                            uint16_t shortUs, uint16_t longUs, uint16_t guardUs,
                            uint8_t repeats) {
    resetRmt();
    assert(radio.transmitPT2262(data, bits, shortUs, longUs, guardUs, repeats));
    cleaned();
    assert(rmtWriteCalls == 1); // Continuous frames even above the generic batch size.

    struct Segment { bool level; uint32_t duration; };
    std::vector<Segment> decoded;
    auto append = [&](bool level, uint16_t duration) {
        if (decoded.empty() || decoded.back().level != level) decoded.push_back({level, duration});
        else decoded.back().duration += duration;
    };
    for (const auto symbol : rmtWrites[0]) {
        assert(symbol.duration0 && symbol.duration1);
        append(symbol.level0, symbol.duration0);
        append(symbol.level1, symbol.duration1);
    }

    assert(decoded.size() == size_t(2 * bits + 2) * repeats);
    size_t i = 0;
    for (uint8_t rep = 0; rep < repeats; ++rep) {
        for (uint16_t bit = 0; bit < bits; ++bit) {
            const bool one = (data[bit / 8] & (0x80U >> (bit % 8))) != 0;
            assert(decoded[i].level && decoded[i].duration == (one ? longUs : shortUs));
            ++i;
            assert(!decoded[i].level && decoded[i].duration == (one ? shortUs : longUs));
            ++i;
        }
        assert(decoded[i].level && decoded[i].duration == shortUs);
        ++i;
        const uint32_t expected = guardUs ? guardUs : 15000;
        // An odd number of RMT atoms needs one terminal low tick; there is no
        // padding between frames.
        assert(!decoded[i].level && decoded[i].duration >= expected &&
               decoded[i].duration <= expected + (rep == repeats - 1 ? 1 : 0));
        ++i;
    }
}
int main() {
    assert(radio.begin(42,41,37,38,39,40));
    radio.setFrequency(315000000);
    const std::array<uint8_t,3> freq{SPI.regs[0x0d],SPI.regs[0x0e],SPI.regs[0x0f]};
    // Odd/even atoms, zeros, 15-bit split boundaries, both modulation paths.
    for (uint16_t duration : {uint16_t(1),uint16_t(32767),uint16_t(32768),uint16_t(65535)}) {
        for (uint8_t mod : {uint8_t(TXMOD_OOK),uint8_t(TXMOD_2FSK_47K6)}) {
            resetRmt();
            const uint16_t d[]{0,duration,120}; const bool lv[]{false,true,false};
            assert(radio.transmitPulses(d,lv,3,3,7,mod));
            cleaned(); assert(rmtWriteCalls == 1);
            auto& items = rmtWrites[0];
            uint64_t high = 0;
            for (auto item : items) {
                if (item.level0) high += item.duration0;
                if (item.level1) high += item.duration1;
            }
            assert(high == uint64_t(duration)*3);
            assert(!items.back().level1);
            assert(waveformDuration(items) >= uint64_t(duration+120)*3+1000);
            assert((std::array<uint8_t,3>{SPI.regs[0x0d],SPI.regs[0x0e],SPI.regs[0x0f]})==freq);
        }
    }
    // Full recording with long silence exceeds the aggregate allocation limit.
    resetRmt(); uint16_t longRaw[1024]; bool levels[1024];
    for (int i=0;i<1024;++i) { longRaw[i]=60000; levels[i]=(i&1)==0; }
    assert(radio.transmitPulses(longRaw,levels,1024,3));
    cleaned(); assert(rmtWriteCalls==3);
    for (auto& items : rmtWrites) assert(waveformDuration(items)==61442000);
    const uint16_t d[]{500,1500}; const bool lv[]{true,false};
    // A failed aggregate submission is a failure, never retried as a whole new waveform.
    resetRmt(); rmtRejectWrite=1;
    assert(!radio.transmitPulses(d,lv,2,15)); cleaned(); assert(rmtWriteCalls==1);
    // Stop the software repeat loop on its first failed write.
    resetRmt(); rmtRejectWrite=2;
    assert(!radio.transmitPulses(longRaw,levels,1024,3)); cleaned(); assert(rmtWriteCalls==2);
    // Completion event never arrives: bound the wait to waveform duration plus 500 ms.
    resetRmt(); rmtNeverComplete=true; uint64_t before=radioClockUs;
    assert(!radio.transmitPulses(d,lv,2,1)); cleaned();
    assert(radioClockUs-before < 520000 && radioClockUs-before >= 503000);
    // Cancel while hardware retains the payload, then successfully reuse the driver.
    resetRmt(); rmtNeverComplete=true;
    rmtPollHook=[](){radio.cancelTransmit();}; before=radioClockUs;
    assert(!radio.transmitPulses(longRaw,levels,1024,15)); cleaned();
    assert(rmtWriteCalls==1 && radioClockUs-before<20000);
    resetRmt(); assert(radio.transmitPulses(d,lv,2)); cleaned();
    resetRmt(); radio.cancelTransmit();
    assert(!radio.transmitPulses(d,lv,2)); cleaned(); assert(rmtWriteCalls==0);
    resetRmt(); failRmtAlloc=true;
    assert(!radio.transmitPulses(d,lv,2)); cleaned(); assert(rmtWriteCalls==0);
    resetRmt(); rmtInitFails=true;
    assert(!radio.transmitPulses(d,lv,2)); assert(!rmtActive && rmtWriteCalls==0);
    // Learned 433.92 MHz and 418 MHz records, including a 56ms guard
    // greater than the RMT per-half-symbol duration limit.
    const uint8_t learned433[]{0x97, 0xB6, 0x28};
    const uint8_t learned418[]{0x9B, 0x06, 0x6C, 0x61, 0x70};
    checkStoredCode(learned433, 24, 325, 937, 8752, 15);
    checkStoredCode(learned418, 39, 304, 609, 56002, 15);
    checkStoredCode(learned418, 39, 304, 609, 56002, 50);
    checkStoredCode(learned433, 24, 325, 937, 0, 2);
    resetRmt(); failRmtAlloc = true;
    assert(!radio.transmitPT2262(learned418, 39, 304, 609, 56002, 50));
    cleaned(); assert(rmtWriteCalls == 0);
    resetRmt();
    assert(!radio.transmitPT2262(learned433, 257, 325, 937, 8752, 15));
    assert(rmtWriteCalls == 0 && !rmtActive);
    // Fixed code shares the bounded RMT lifecycle and propagates errors too.
    const uint8_t key[]{0xa5};
    resetRmt(); assert(radio.transmitPT2262(key,8,300,900,9600,15)); cleaned();
    resetRmt(); rmtRejectWrite=1;
    assert(!radio.transmitPT2262(key,8,300,900,9600,15)); cleaned();
    std::cout << "PASS: RAW/fixed-code waveform, long recording, timeout, cancellation, retry and cleanup\n";
}
