#pragma once
#include "RFSignalDecoder.h"
class RadioHelper {
    RFSignalDecoder decoder;
public:
    std::atomic<int16_t> rssi{-110};
    bool receiving = false;
    bool EnableRawReceive(float, uint8_t) {
        decoder.begin(39, 40);
        decoder.enableRawMode(true);
        receiving = true;
        return true;
    }
    void QuiesceRawReceive() { decoder.enableRawMode(false); }
    void DisableRawReceive() { decoder.enableRawMode(false); receiving = false; }
    int16_t GetRawRssi() { return rssi; }
    RFSignalDecoder& GetDecoder() { return decoder; }
};
