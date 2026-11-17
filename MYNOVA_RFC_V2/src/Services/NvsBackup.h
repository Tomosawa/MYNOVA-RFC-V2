#pragma once

#include <stddef.h>
#include <stdint.h>

// Typed records for application-owned namespaces on the default NVS partition.
// This stays independent of SDK Wi-Fi state, calibration, and device identity.
class NvsBackup {
public:
    static constexpr uint8_t Signals = 1;
    static constexpr uint8_t Settings = 2;
    static constexpr uint8_t All = Signals | Settings;
    static constexpr size_t Capacity = 32 * 1024; // The NVS partition is 20 KiB.

    NvsBackup();
    ~NvsBackup();
    NvsBackup(const NvsBackup&) = delete;
    NvsBackup& operator=(const NvsBackup&) = delete;

    uint8_t* data;
    size_t size = 0;
    bool capture(uint8_t sections);
    bool validate(uint8_t sections) const;
    bool canReplace(uint8_t sections) const;
    bool replace(uint8_t sections) const;
    struct RawSlot { bool present = false; uint64_t fileId = 0; };
    bool rawSlots(RawSlot* slots) const;
    bool setRawFileId(int slot, uint64_t fileId);
};
