#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
using String = std::string;
inline uint32_t uiTime=0;
uint32_t millis() {return uiTime;}
enum class RawTransmitState : uint8_t {Idle,Sending,Sent,Failed,Cancelled};
struct {
    RawTransmitState result=RawTransmitState::Idle;
    bool cancel=false, startOk=true;
    int starts=0;
    bool StartRawTransmit() {
        ++starts;
        if (!startOk || result==RawTransmitState::Sending) return false;
        result=RawTransmitState::Sending; cancel=false; return true;
    }
    RawTransmitState GetRawTransmitState() const {return result;}
    void CancelRawTransmit() {cancel=true;}
} radioHelper;
struct {int backs=0; void navigateBack() {++backs;}} uiEngine;
struct Nav {
    String left,middle,right;
    void setLeftButtonText(const char* s) {left=s;}
    void setMiddleButtonText(const char* s) {middle=s;}
    void setRightButtonText(const char* s) {right=s;}
};
enum RawPageState {RAW_PAGE_START,RAW_PAGE_REC,RAW_PAGE_IDLE,RAW_PAGE_SENDING};
struct RawRecordPage {
    struct {
        bool data=true, recording=false;
        int erases=0;
        bool hasData() {return data;}
        struct Snapshot {bool recording;};
        Snapshot snapshot() {return {recording};}
        void erase() {++erases;data=false;}
    } session;
    RawPageState state=RAW_PAGE_IDLE;
    int saves=0, records=0;
    String notice;
    uint32_t noticeUntil=0;
    Nav nav; Nav* navBar=&nav;
    void beginRecord() {++records;state=RAW_PAGE_REC;}
    void stopRecord() {state=RAW_PAGE_IDLE;}
    void saveRecord() {++saves;}
    void eraseRecord() {session.erase();state=RAW_PAGE_START;}
    void update();
    void updateButtons();
    void sendRecord();
    void onButtonBack(void*);
    void onButtonEnter(void*);
    void onButtonMenu(void*);
    void onButton4(void*);
    void onButton6(void*);
};
struct SendDataPage {
    bool rawSending=true, bAnimating=true;
    Nav nav; Nav* detailNavBar=&nav;
    void update();
};
#include "raw_transmit_ui.inc"
int main() {
    RawRecordPage page;
    page.onButtonEnter(nullptr);
    assert(page.state==RAW_PAGE_SENDING && page.notice=="发送中");
    assert(page.nav.left=="停止" && page.nav.right=="停止" && page.nav.middle.empty());
    for (int i=0;i<100;++i) {uiTime+=16;page.update();}
    assert(page.state==RAW_PAGE_SENDING); // UI keeps processing frames while worker runs.
    page.onButtonMenu(nullptr);page.onButton6(nullptr);page.onButton4(nullptr);
    assert(page.saves==0 && page.session.erases==0);
    page.onButtonBack(nullptr);
    assert(radioHelper.cancel && uiEngine.backs==0 && page.session.erases==0);
    assert(page.notice=="停止中");
    page.onButtonEnter(nullptr);assert(radioHelper.starts==1); // Stop, never queue another send.
    radioHelper.result=RawTransmitState::Cancelled; page.update();
    assert(page.state==RAW_PAGE_IDLE && page.notice=="已停止" && page.noticeUntil==uiTime+2000);
    page.onButtonMenu(nullptr);assert(page.saves==1);
    page.onButtonEnter(nullptr); radioHelper.result=RawTransmitState::Sent; page.update();
    assert(page.notice=="已发送" && page.state==RAW_PAGE_IDLE);
    page.onButtonEnter(nullptr); radioHelper.result=RawTransmitState::Failed; page.update();
    assert(page.notice=="发送失败" && page.state==RAW_PAGE_IDLE);
    radioHelper.startOk=false; page.onButtonEnter(nullptr);
    assert(page.state==RAW_PAGE_IDLE && page.notice=="发送失败");
    page.onButtonBack(nullptr); assert(uiEngine.backs==1 && page.session.erases==1);
    SendDataPage saved;
    radioHelper.result=RawTransmitState::Sending;saved.update();assert(saved.bAnimating && saved.rawSending);
    for (auto result : {RawTransmitState::Sent,RawTransmitState::Cancelled,RawTransmitState::Failed}) {
        saved.bAnimating=saved.rawSending=true;radioHelper.result=result;saved.update();
        assert(!saved.bAnimating && !saved.rawSending && saved.nav.left=="返回" && saved.nav.right=="发送");
        assert(saved.nav.middle==(result==RawTransmitState::Sent ? "发送成功" :
                                result==RawTransmitState::Cancelled ? "已停止" : "发送失败"));
    }
    std::cout << "PASS: production RAW UI handles frames/stop, protects recording, reports completion/failure and permits retry\n";
}
