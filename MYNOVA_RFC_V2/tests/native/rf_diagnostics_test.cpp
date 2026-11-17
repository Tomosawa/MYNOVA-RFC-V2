#include "CC1101Driver.h"
#include "FrequencyScanner.h"
#include "../../src/Util/Log.h"
#include "fixtures/rx_433_success.h"
#include <iostream>

// Count calls into the serial logger. Actual acquisition/decoder/scanner remain
// production code; serial backpressure must never be reached by INFO diagnostics.
std::atomic<LogLevel> Log::s_level{LogLevel::INFO};
void* Log::s_mutex = nullptr;
static unsigned logCalls = 0;
LogLevel Log::getLevel() { return s_level; }
void Log::setLevel(LogLevel level, bool) { s_level = level; }
void Log::output(const char*, LogLevel level, const char*, ...) {
    if (level <= s_level) logCalls++;
}

int main() {
    CC1101Driver driver;RFSignalDecoder decoder;
    assert(driver.begin(42,41,2,38,39,40));
    FrequencyScanner scanner(driver,decoder);float freq=433.92f;
    for(auto level : {LogLevel::INFO,LogLevel::DEBUG}) {
        modelDelayTime=true;decoder.begin(39,40);assert(scanner.begin(&freq,1));
        modelDelayTime=false;Log::setLevel(level,false);logCalls=0;
        uint64_t lastEdge=radioClockUs;
        auto edge=[&](uint32_t duration,bool nextLevel) {
            lastEdge+=duration;assert(lastEdge>=radioClockUs);radioClockUs=lastEdge;
            pinLevels[40]=nextLevel;pinIsr[40]();scanner.loop();
        };
        edge(60000,false);
        for(int frame=0;frame<2;frame++) {
            const int32_t* data=frame?success433_2:success433_1;
            for(int i=frame?1:0;i<51;i++) edge(abs(data[i]),data[i]<0);
            radioClockUs+=30;scanner.loop();
        }
        SignalFrame confirmed{};assert(scanner.getConfirmedSignal(confirmed));
        assert(confirmed.bitCount==24 && confirmed.data[0]==0x97 &&
               confirmed.data[1]==0xb6 && confirmed.data[2]==0x28);
        decoder.logReceiveDiagnostics(true);
        if(level==LogLevel::INFO) assert(logCalls==0);
        else assert(logCalls>10);
        scanner.stop();
    }
    std::cout << "PASS: production real-frame replay and diagnostics call no serial logger at INFO; DEBUG retains pulse/protocol output\n";
}
