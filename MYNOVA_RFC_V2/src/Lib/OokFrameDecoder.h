#pragma once

#include <cstdint>

// 通用 OOK PWM 以两个实测同步边界确定帧长，匹配两帧后确认。
class OokFrameDecoder {
public:
    struct Frame {
        uint8_t data[32]{};
        uint16_t bits = 0, shortUs = 0, longUs = 0, syncUs = 0;
    };

    void reset();
    uint8_t feed(const uint16_t* widths, const bool* levels, uint16_t count,
                 bool continuous, bool final, Frame& output);

private:
    static constexpr uint16_t Capacity = 512;
    static constexpr uint16_t MaxWidth = 65000;
    uint16_t width[Capacity]{};
    bool level[Capacity]{};
    uint16_t used = 0, checked = 0;
    int lastSync = -1;
    Frame previous{};
    uint8_t repeats = 0;

    void resetStream() { used = checked = 0; lastSync = -1; }
    void keepTail();
    void dropPrefix(uint16_t count);
    bool combine(uint16_t index, uint32_t sum, uint8_t& found, Frame& output);
    void inspect(bool final, uint8_t& found, Frame& output);
    void estimateStreamTiming(uint16_t& shortUs, uint16_t& longUs) const;
    bool decode(uint16_t start, uint16_t end, Frame& frame) const;
};
