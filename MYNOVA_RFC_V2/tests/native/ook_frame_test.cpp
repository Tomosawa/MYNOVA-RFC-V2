#include "OokFrameDecoder.h"
#include "fixtures/rx_433_success.h"
#include "fixtures/rx_418_observed.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <iterator>
#include <vector>

static uint8_t feed(OokFrameDecoder& decoder, const std::vector<int32_t>& pulse,
                    uint16_t chunkSize, OokFrameDecoder::Frame& frame) {
    uint8_t confirmed = 0;
    for (size_t offset = 0; offset < pulse.size(); offset += chunkSize) {
        uint16_t widths[48];
        bool levels[48];
        uint16_t count = std::min<size_t>(chunkSize, pulse.size() - offset);
        for (uint16_t i = 0; i < count; ++i) {
            const int32_t value = pulse[offset + i];
            levels[i] = value > 0;
            widths[i] = levels[i] ? value : -value;
        }
        const uint8_t found = decoder.feed(widths, levels, count, offset != 0,
                                           offset + count == pulse.size(), frame);
        if (found) confirmed = found;
    }
    return confirmed;
}

int main() {
    const uint8_t code433[] = {0x97, 0xB6, 0x28};
    const uint8_t code418[] = {0x9B, 0x06, 0x6C, 0x61, 0x70};
    std::vector<int32_t> capture433(std::begin(success433_1), std::end(success433_1));
    // The first trailing guard doubles as the leading guard of the next frame.
    capture433.insert(capture433.end(), std::begin(success433_2) + 1, std::end(success433_2));

    // Extract the 39 real pairs and final short mark following a guard in an
    // older 418 capture. Its isolated snapshot ends before the final guard.
    auto begin418 = std::find(std::begin(observed418_1), std::end(observed418_1), -60000);
    assert(begin418 != std::end(observed418_1));
    assert(std::end(observed418_1) - begin418 == 80);
    std::vector<int32_t> capture418{-56002};
    for (int i = 0; i < 3; ++i) {
        capture418.insert(capture418.end(), begin418 + 1, std::end(observed418_1));
        capture418.push_back(-56002);
    }

    for (uint16_t chunk : {17, 31, 48}) {
        OokFrameDecoder decoder;
        OokFrameDecoder::Frame frame;
        decoder.reset();
        assert(feed(decoder, {std::begin(success433_1), std::end(success433_1)}, chunk, frame) == 0);
        decoder.reset();
        assert(feed(decoder, capture433, chunk, frame) == 2);
        assert(frame.bits == 24 && std::memcmp(frame.data, code433, sizeof(code433)) == 0);
        decoder.reset();
        assert(feed(decoder, capture418, chunk, frame) >= 2);
        assert(frame.bits == 39 && std::memcmp(frame.data, code418, sizeof(code418)) == 0);
    }
    std::cout << "PASS: Shared OOK frame decoder: measured 433/418 timings, two frames and partial blocks\n";
}
