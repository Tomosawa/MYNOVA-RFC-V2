// Host preview of production drawing functions, using the actual U8g2 C renderer/fonts.
#include "Arduino.h"
#include "RawCaptureSession.h"
#include "u8g2.h"
#include <cassert>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

class String {
    std::string value;
public:
    String() = default;
    String(const char* s): value(s) {}
    String(std::string s): value(std::move(s)) {}
    String(int n): value(std::to_string(n)) {}
    String(float n, unsigned digits) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(digits) << n;
        value = out.str();
    }
    const char* c_str() const { return value.c_str(); }
    unsigned length() const { return value.size(); }
    friend String operator+(const String& a, const String& b) { return a.value + b.value; }
};
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

class U8G2 {
public:
    u8g2_t g{};
    std::vector<int> textBaselines;
    U8G2() {
        u8g2_Setup_ssd1306_128x64_noname_f(&g, U8G2_R0,
            [](u8x8_t*, uint8_t, uint8_t, void*)->uint8_t { return 1; },
            [](u8x8_t*, uint8_t, uint8_t, void*)->uint8_t { return 1; });
        u8g2_ClearBuffer(&g);
    }
    void setDrawColor(int c) { u8g2_SetDrawColor(&g, c); }
    void setFont(const uint8_t* f) { u8g2_SetFont(&g, f); }
    void setFontMode(int m) { u8g2_SetFontMode(&g, m); }
    int getMaxCharHeight() { return u8g2_GetMaxCharHeight(&g); }
    int getUTF8Width(const char* s) { return u8g2_GetUTF8Width(&g, s); }
    int getStrWidth(const char* s) { return u8g2_GetStrWidth(&g, s); }
    void drawUTF8(int x, int y, const char* s) {
        assert(x >= 0 && x + getUTF8Width(s) <= 128 && y < 64);
        if (!strcmp(s, "频率") || !strcmp(s, "调制") || !strcmp(s, "门限"))
            assert(g.draw_color == 1); // 回归：左侧标签不得继承参数反色
        textBaselines.push_back(y);
        u8g2_DrawUTF8(&g, x, y, s);
    }
    void drawStr(int x, int y, const char* s) {
        assert(x >= 0 && x + getStrWidth(s) <= 128 && y < 64);
        textBaselines.push_back(y);
        u8g2_DrawStr(&g, x, y, s);
    }
    void drawFrame(int x, int y, int w, int h) { u8g2_DrawFrame(&g, x, y, w, h); }
    void drawHLine(int x, int y, int w) { u8g2_DrawHLine(&g, x, y, w); }
    void drawVLine(int x, int y, int h) {
        assert(x >= 0 && x < 128 && y >= 0 && y + h <= 64);
        u8g2_DrawVLine(&g, x, y, h);
    }
    void drawPixel(int x, int y) { u8g2_DrawPixel(&g, x, y); }
    void drawRBox(int x, int y, int w, int h, int r) { u8g2_DrawRBox(&g, x, y, w, h, r); }
    void save(const std::string& path) {
        std::ofstream out(path, std::ios::binary);
        out << "P5\n128 64\n255\n";
        auto* pixels = u8g2_GetBufferPtr(&g);
        for (unsigned y = 0; y < 64; ++y)
            for (unsigned x = 0; x < 128; ++x)
                out.put(static_cast<char>(pixels[x + (y / 8) * 128] & (1 << (y % 8)) ? 255 : 0));
    }
};
class UINavBar {
public:
    int x, y, width, height, marginLeft, marginRight, lineHeight;
    bool bVisible = true, bShowBorder, bLeftHighLight, bMiddleHighLight, bRightHighLight;
    String leftButtonText, middleButtonText, rightButtonText;
    const uint8_t* buttonFont = u8g2_font_wqy12_t_gb2312;
    UINavBar(int, int, int, int);
    void render(U8G2*, int = 0, int = 0);
    void setLeftButtonText(String s) { leftButtonText = s; }
    void setMiddleButtonText(String s) { middleButtonText = s; }
    void setRightButtonText(String s) { rightButtonText = s; }
};
class UIPage {
public:
    UINavBar* navBar;
    void render(U8G2* g) { navBar->render(g); }
};
struct PreviewSession {
    RawCaptureSnapshot s;
    RawCaptureSnapshot snapshot() const { return s; }
    bool hasData() const { return s.samples && !s.writeError; }
};
enum RawPageState { RAW_PAGE_START, RAW_PAGE_REC, RAW_PAGE_IDLE };
class RawRecordPage: public UIPage {
public:
    RawCaptureConfig cfg;
    PreviewSession session;
    RawPageState state = RAW_PAGE_START;
    String notice;
    uint32_t noticeUntil = 0;
    String topInfo() const;
    void drawGraph(U8G2*, const RawCaptureSnapshot&);
    void render(U8G2*);
    void updateButtons();
};
const float RF_FREQ_TABLE_ALL[] = {310,315,330,350,390,418,430.5,431.5,432.8,433.92};
class RawConfigPage: public UIPage {
public:
    uint8_t selRow = 0, freqIdx = 9, presetIdx = 0, thresholdIdx = 5;
    static const uint8_t ROW_COUNT = 3;
    String freqText() const;
    String presetText() const;
    String thresholdText() const;
    void render(U8G2*);
};
static const int GRAPH_Y0 = 12, GRAPH_Y1 = 38;
#include "drawing_functions.inc"

int main(int argc, char** argv) {
    assert(argc == 2);
    std::string directory = argv[1];
    for (unsigned row = 0; row < 3; ++row) {
        U8G2 g;
        RawConfigPage page;
        UINavBar nav(0,52,128,12);
        nav.marginLeft = nav.marginRight = 0;
        nav.leftButtonText = "返回";
        nav.rightButtonText = "开始";
        page.navBar = &nav;
        page.selRow = row;
        page.render(&g);
        page.navBar = nullptr;
        g.save(directory + "/config_" + std::to_string(row) + ".pgm");
    }
    // 检查每个调制/门限值的实际字体宽度，不得跑出屏幕。
    for (unsigned p = 0; p < 4; ++p) for (unsigned t = 0; t < sizeof(RAW_THRESHOLDS)/sizeof(RAW_THRESHOLDS[0]); ++t) {
        U8G2 g;
        RawConfigPage page;
        UINavBar nav(0,52,128,12);
        page.navBar = &nav;
        page.presetIdx = p; page.thresholdIdx = t;
        page.render(&g);
    }
    for (const auto& scene: {"waiting", "recording", "stopped", "full", "error"}) {
        U8G2 g;
        RawRecordPage page;
        UINavBar nav(0,52,128,12);
        page.navBar = &nav;
        auto& s = page.session.s;
        page.state = RAW_PAGE_REC;
        s.elapsedMs = 12400; s.rssi = -102; s.head = 80;
        for (unsigned i = 0; i < 80; ++i) s.history[i] = i % 7 == 0 ? 3 : 2;
        page.cfg.rssiThreshold = -85;
        if (std::string(scene) != "waiting") {
            s.samples = 684; s.rssi = -58; s.gateOpen = true; s.edges = 720;
            s.wrapped = true; s.head = 35;
            for (unsigned i = 0; i < 100; ++i)
                s.history[(35+i)%100] = i > 30 && i < 65 ? 12 + i % 10 : 2 + i % 3;
        }
        if (std::string(scene) == "stopped") page.state = RAW_PAGE_IDLE;
        if (std::string(scene) == "full") { page.state = RAW_PAGE_IDLE; s.full = true; s.samples = 1024; }
        if (std::string(scene) == "error") { page.state = RAW_PAGE_IDLE; s.writeError = true; }
        page.updateButtons();
        page.render(&g);
        for (auto y: g.textBaselines) assert(y == 8 || y == 49 || y == 62);
        g.save(directory + "/" + scene + ".pgm");
    }
    puts("PASS: actual-font bounds, graph-only region, config + five capture states");
}
