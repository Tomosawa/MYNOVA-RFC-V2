/*
  RawRecordPage.h — SubGHz RAW 录制/回放页（对齐 Flipper Read RAW）

  状态：开始 → 录制中 → 停止(可发送/保存/清除)
  界面：顶部频率/调制/时间；独立RSSI图；状态/RSSI/段数；实体按键。
*/

#ifndef RAW_RECORD_PAGE_H
#define RAW_RECORD_PAGE_H

#include "../../GUI/UIPage.h"
#include "../../GUI/Widget/UINavBar.h"
#include "../../Lib/RawCaptureSession.h"

enum RawPageState {
    RAW_PAGE_START = 0,
    RAW_PAGE_REC,
    RAW_PAGE_IDLE,
    RAW_PAGE_SENDING
};

class RawRecordPage : public UIPage {
public:
    explicit RawRecordPage(const RawCaptureConfig& cfg);
    ~RawRecordPage();

    void render(U8G2* u8g2) override;
    void showPage() override;
    void update() override;

    void onButtonBack(void* context = nullptr) override;
    void onButtonEnter(void* context = nullptr) override;  // 录制/停止/发送
    void onButtonMenu(void* context = nullptr) override;   // IDLE：保存
    void onButton5(void* context = nullptr) override;
    void onButton4(void* context = nullptr) override;      // IDLE：清除
    void onButton6(void* context = nullptr) override;      // IDLE：保存

private:
    RawCaptureConfig cfg;
    RawCaptureSession session;
    RawPageState state = RAW_PAGE_START;
    bool firstShow = true;
    String notice;
    uint32_t noticeUntil = 0;

    UINavBar* navBar = nullptr;

    void beginRecord();
    void stopRecord();
    void sendRecord();
    void saveRecord();
    void eraseRecord();

    void updateButtons();
    void drawGraph(U8G2* u8g2, const RawCaptureSnapshot& snapshot);
    String topInfo() const;
};

#endif // RAW_RECORD_PAGE_H
