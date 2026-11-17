#include "RFSignalDecoder.h"
#include <cassert>
#include <iostream>
#include <vector>

static void edge(RFSignalDecoder& d, uint32_t duration, bool level) {
    testMicros += duration;
    testLevel = level;
    d.processInterrupt();
}
static std::vector<int> drain(RFSignalDecoder& d) {
    std::vector<int> result;
    RawSeg batch[128];
    for (;;) {
        auto n = d.rawDrain(batch, 128);
        if (!n) return result;
        for (unsigned i = 0; i < n; ++i)
            result.push_back(batch[i].level ? batch[i].durUs : -batch[i].durUs);
    }
}
int main() {
    RFSignalDecoder d;
    d.begin(39, 40);
    d.enableRawMode(true);
    edge(d, 5000000, true); // 等待5秒后首边沿：不能录成几百段静默
    edge(d, 320, false);
    edge(d, 960, true);
    edge(d, 320, false);
    d.enableRawMode(false); // 停止时必须提交最后一个高电平
    assert((drain(d) == std::vector<int>{320, -960, 320}));

    for (bool gateClose : {false, true}) {
        d.enableRawMode(true);
        edge(d, 5000000, true);
        edge(d, 320, false);
        testMicros += 8752; // Stop during the physical low sync before the next rising edge.
        if (gateClose) d.rawSetGate(false);
        else d.finishRawCapture();
        assert((drain(d) == std::vector<int>{320, -8752}));
    }

    d.enableRawMode(true);
    edge(d, 1000000, true);
    edge(d, 300, false);
    edge(d, 20, true); // 短毛刺并入前段，再与同极性有效段合并
    edge(d, 300, false);
    edge(d, 900, true);
    d.enableRawMode(false);
    assert((drain(d) == std::vector<int>{620, -900}));

    d.enableRawMode(true);
    edge(d, 1000000, true);
    edge(d, 33, false);
    edge(d, 66, true);
    edge(d, 33, false);
    d.enableRawMode(false);
    assert((drain(d) == std::vector<int>{33, -66, 33}));

    d.enableRawMode(true);
    d.rawSetGate(false);
    edge(d, 100, true);
    edge(d, 300, false);
    assert(d.rawEdgeCount() == 2); // 噪声仍计入诊断边沿，不计入文件
    assert(drain(d).empty());
    d.rawSetGate(true);
    edge(d, 5000000, true);
    edge(d, 400, false);
    edge(d, 1200, true);
    d.rawSetGate(false); // RSSI跌落后，已接受脉冲必须仍能被排空
    edge(d, 300, false);
    assert((drain(d) == std::vector<int>{400, -1200}));

    d.enableRawMode(true);
    edge(d, 100, true);
    edge(d, 130000, false); // 超过16bit段长时拆分，保留总时长和电平
    d.enableRawMode(false);
    assert((drain(d) == std::vector<int>{60000, 60000, 10000}));

    d.enableRawMode(true);
    edge(d, 100, true);
    for (unsigned i = 0; i < RAW_RING_SIZE * 2 + 200; ++i) {
        edge(d, 300, i % 2 != 0);
        if (i % 120 == 119) drain(d); // 多次环形绕回，不得误报溢出
    }
    d.enableRawMode(false);
    assert(!d.rawTakeOverrun());

    d.enableRawMode(true);
    edge(d, 100, true);
    for (unsigned i = 0; i < RAW_RING_SIZE + 100; ++i) edge(d, 300, i % 2 != 0);
    assert(d.rawTakeOverrun());
    assert(!d.rawTakeOverrun());
    assert(drain(d).size() == RAW_RING_SIZE - 1);
    std::cout << "PASS: silence, polarity, tail, glitches, RSSI gate, long segments, wrap, overrun\n";
}
