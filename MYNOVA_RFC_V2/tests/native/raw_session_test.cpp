#include "RawCaptureSession.h"
#include "RadioHelperStub.h"
#include "LittleFS.h"
#include <cassert>
#include <iostream>
#include <sstream>
#include <vector>

static void edge(RadioHelper& r, unsigned duration, bool level) {
    testMicros += duration;
    testLevel = level;
    r.GetDecoder().processInterrupt();
}
template<class Predicate> static void until(Predicate predicate) {
    auto deadline = millis() + 2000;
    while (!predicate() && millis() < deadline) vTaskDelay(1);
    assert(predicate());
}
static std::vector<int> saved() {
    std::istringstream in(*testFiles.at(RAW_TEMP_FILE));
    std::string line;
    std::vector<int> values;
    while (std::getline(in, line)) {
        if (line.rfind("RAW_Data:", 0) != 0) continue;
        std::istringstream row(line.substr(9));
        int value;
        while (row >> value) values.push_back(value);
    }
    return values;
}
int main() {
    RadioHelper radio;
    RawCaptureConfig cfg;
    cfg.rssiThreshold = -85;
    RawCaptureSession session;
    assert(session.start(radio, cfg));
    for (unsigned i = 0; i < 200; ++i) edge(radio, 300, i % 2 != 0);
    vTaskDelay(5);
    assert(session.snapshot().samples == 0); // 弱信号不能灌满文件
    radio.rssi = -60;
    until([&]{ return session.snapshot().gateOpen; });
    edge(radio, 5000000, true);
    for (unsigned i = 0; i < 60; ++i) edge(radio, i % 2 ? 960 : 320, i % 2 != 0);
    radio.rssi = -110; // 接收后RSSI跌落，已入环数据仍需保存
    until([&]{ return !session.snapshot().gateOpen; });
    session.stop();
    assert(saved().size() == 60);
    for (unsigned i = 0; i < 60; ++i) assert(saved()[i] == (i % 2 ? -960 : 320));
    assert(session.snapshot().elapsedMs >= RAW_GATE_HOLD_MS);
    auto history = session.snapshot();
    assert(*std::max_element(std::begin(history.history), std::end(history.history)) ==
           RawCaptureSession::rssiToBar(-60)); // 短突发峰值不能被50ms图表采样漏掉
    auto frozen = session.snapshot().elapsedMs;
    vTaskDelay(5);
    assert(session.snapshot().elapsedMs == frozen);
    assert(testFiles.at(RAW_TEMP_FILE)->find("Frequency: 433920000\n") != std::string::npos);

    cfg.rssiThreshold = RAW_THRESHOLD_OFF;
    assert(session.start(radio, cfg));
    edge(radio, 1000000, true);
    for (unsigned i = 0; i < 700; ++i) edge(radio, 300, i % 2 != 0);
    session.stop(); // 不依赖UI render/poll，停止立即排空超过一批的尾段
    assert(saved().size() == 700);
    assert(!session.snapshot().overrun);
    session.erase();
    auto empty = session.snapshot();
    assert(empty.samples == 0 && empty.head == 0 && !empty.wrapped && empty.elapsedMs == 0);

    assert(session.start(radio, cfg));
    edge(radio, 1000000, true);
    for (unsigned i = 0; i < 60000; ++i) {
        edge(radio, 300, i % 2 != 0);
        if (i % 100 == 99) vTaskDelay(3);
    }
    session.stop();
    assert(!session.snapshot().full && !session.snapshot().overrun);
    assert(saved().size() == 60000 && session.hasData()); // 远超旧1024/中间16384限制

    assert(session.start(radio, cfg));
    testWriteBudget = 0;
    edge(radio, 1000000, true);
    for (unsigned i = 0; i < 600; ++i) edge(radio, 300, i % 2 != 0);
    until([&]{ return !session.snapshot().recording; });
    session.stop();
    assert(session.snapshot().writeError && !session.hasData());
    testWriteBudget = -1;

    testTaskCreateFailure = true;
    assert(!session.start(radio, cfg));
    assert(!radio.receiving);
    assert(!session.snapshot().recording);
    testTaskCreateFailure = false;
    testMountOk = false;
    assert(!session.start(radio, cfg));
    testMountOk = true;
    std::cout << "PASS: background capture, RSSI tail, 700-segment stop, elapsed time, save, erase, 60000-segment continuous recording, disk/task failures\n";
}
