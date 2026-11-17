#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <cstdint>
#define LOG_I(...)
#define LOG_E(...)
#define RF_SLOT_COUNT 100
#define RAW_TEMP_FILE "/raw_tmp.sub"
#include "TaskConfig.h"
constexpr int pdPASS = 1;
inline void(*pendingTask)(void*) = nullptr;
inline void* pendingContext = nullptr;
inline bool failTaskCreate = false;
inline int beeps = 0;
void SoundAlertBeep(int) { ++beeps; }
int xTaskCreatePinnedToCore(void(*entry)(void*), const char* name, int stack, void* context,
                           int priority, void*, int core) {
    assert(strcmp(name,"RawTx")==0 && stack==4096 && core==TASK_CORE_APP && priority==TASK_PRIO_HW);
    if (failTaskCreate) return 0;
    pendingTask=entry; pendingContext=context; return pdPASS;
}
void vTaskDelete(void*) {}
enum class RawTransmitState : uint8_t { Idle, Sending, Sent, Failed, Cancelled };
#include "FlipperSignalFile.h"
#include "RFSubGhzPresets.h"
constexpr uint8_t TXMOD_OOK=0, TXMOD_2FSK_2K38=1, TXMOD_2FSK_47K6=2;
struct CameStyleProtoConst {const char* name="CAME";};
const CameStyleProtoConst* findCameStyleConst(const char*) {return nullptr;}
bool subGhzRecordToSignalEntry(const SubGhzKeyRecord&,SignalEntry&) {return false;}
struct CameStyleProtocolDecoder {
    static uint16_t buildFrame(const CameStyleProtoConst*,uint64_t,uint8_t,uint16_t,uint16_t*,bool*) {return 0;}
};
struct RadioHelper {
    bool cc1101Ready=true, receiving=true, rawReceiving=true, sendOk=true;
    std::atomic<bool> transmitBusy{false};
    std::atomic<RawTransmitState> rawTransmitState{RawTransmitState::Idle};
    int rawTransmitSlot=0, sends=0, repeatCount=15, txPowerLevel=7;
    bool bReceiveMode=false, bRawMode=false;
    int lastSlot = -1;
    struct {
        bool cancelled=false;
        int pulseCalls=0, restores=0;
        uint8_t repeat=0, mod=0;
        uint32_t firstDuration=0;
        bool firstLevel=false;
        bool setFrequency(uint32_t) {return true;}
        void restoreDefaultConfig() {++restores;}
        bool transmitPulses(const uint16_t* d,const bool* lv,uint16_t count,uint8_t r,int,uint8_t m=0) {
            assert(count<=SUB_RAW_MAX_PULSES); ++pulseCalls; repeat=r; mod=m;
            firstDuration=d[0]; firstLevel=lv[0]; return !cancelled;
        }
        bool transmitRaw(const int32_t* pulses,uint16_t count,int,uint8_t preset) {
            assert(count<=SUB_RAW_MAX_PULSES);++pulseCalls;repeat=1;
            mod=preset==RF_PRESET_FM476 ? TXMOD_2FSK_47K6 :
                preset==RF_PRESET_FM238 ? TXMOD_2FSK_2K38 : TXMOD_OOK;
            int64_t v=pulses[0];firstDuration=v<0 ? -v : v;firstLevel=v>0;
            return !cancelled;
        }
        bool transmitPT2262(const uint8_t*,uint16_t,uint16_t,uint16_t,uint16_t,uint8_t,int) {return false;}
        void resetTransmitCancel() {cancelled=false;}
        bool transmitCancelled() const {return cancelled;}
        void cancelTransmit() {cancelled=true;}
    } cc1101;
    void DisableReceive() {receiving=false;}
    void DisableRawReceive() {rawReceiving=false;}
    bool sendRawSlotFile(int slot) {
        assert(!receiving && !rawReceiving);
        lastSlot = slot; ++sends;
        return sendOk && !cc1101.cancelled;
    }
    int bandOfFrequency(float) {return 0;}
    void setAntennaBand(int) {}
    bool sendSubGhzRecord(const SubGhzKeyRecord&);
    bool StartRawTransmit(int);
    static void rawTransmitTask(void*);
};
#include "raw_transmit_task.inc"
static void runWorker() {
    assert(pendingTask); auto task=pendingTask; pendingTask=nullptr;task(pendingContext);
}
int main() {
    RadioHelper helper;
    assert(!helper.StartRawTransmit(-1) && !helper.StartRawTransmit(101));
    helper.cc1101Ready=false; assert(!helper.StartRawTransmit(0)); helper.cc1101Ready=true;
    assert(helper.StartRawTransmit(0));
    assert(helper.rawTransmitState==RawTransmitState::Sending && helper.sends==0);
    assert(!helper.StartRawTransmit(1)); // One worker owns the radio and static buffers.
    runWorker(); assert(helper.rawTransmitState==RawTransmitState::Sent && beeps==1);
    assert(!helper.transmitBusy);
    assert(helper.lastSlot == 0);
    assert(helper.StartRawTransmit(100)); helper.cc1101.cancelTransmit(); runWorker();
    assert(helper.rawTransmitState==RawTransmitState::Cancelled && beeps==1);
    assert(helper.lastSlot == 100);
    assert(helper.StartRawTransmit(2)); assert(!helper.cc1101.cancelled); runWorker();
    assert(helper.rawTransmitState==RawTransmitState::Sent && beeps==2);
    helper.sendOk=false; assert(helper.StartRawTransmit(1)); runWorker();
    assert(helper.rawTransmitState==RawTransmitState::Failed && beeps==2);
    failTaskCreate=true; assert(!helper.StartRawTransmit(0));
    assert(helper.rawTransmitState==RawTransmitState::Failed && !pendingTask);
    assert(!helper.transmitBusy);
    helper.transmitBusy=true; assert(!helper.StartRawTransmit(0)); helper.transmitBusy=false;
    failTaskCreate=false; helper.sendOk=true;
    assert(helper.StartRawTransmit(0)); runWorker(); assert(helper.rawTransmitState==RawTransmitState::Sent);
    SubGhzKeyRecord rec;
    rec.isRaw=true;rec.frequencyHz=433920000;
    strcpy(rec.preset,"FuriHalSubGhzPresetOok650Async");
    strcpy(rec.protocol,"RAW");
    assert(rec.reserveRaw(8));rec.rawCount=8;
    for(int i=0;i<8;++i) rec.rawData[i]=(i%2) ? -1500 : 500;
    assert(helper.sendSubGhzRecord(rec)); assert(helper.cc1101.repeat==1);
    assert(helper.cc1101.mod==TXMOD_OOK && helper.cc1101.firstLevel);
    strcpy(rec.preset,"FuriHalSubGhzPreset2FSKDev476Async");
    assert(helper.sendSubGhzRecord(rec)); assert(helper.cc1101.mod==TXMOD_2FSK_47K6);
    strcpy(rec.preset,"FuriHalSubGhzPreset2FSKDev238Async");
    rec.rawData[0]=INT32_MIN;
    assert(helper.sendSubGhzRecord(rec));
    assert(helper.cc1101.mod==TXMOD_2FSK_2K38 && !helper.cc1101.firstLevel);
    assert(helper.cc1101.firstDuration==2147483648u);
    int calls=helper.cc1101.pulseCalls;
    rec.rawCount=SUB_RAW_MAX_PULSES+1; assert(!helper.sendSubGhzRecord(rec));
    rec.rawCount=0; assert(!helper.sendSubGhzRecord(rec));
    assert(helper.cc1101.pulseCalls==calls);
    std::cout << "PASS: production RAW routing replays once, preserves OOK/FSK, rejects invalid count and preserves full INT32 magnitude\n";
    std::cout << "PASS: background temporary/slot send, busy guard, results, cancellation and task failure recovery\n";
}
