#pragma once

#include <FS.h>

// ID zero refers to a legacy slot in LittleFS. New captures get independent
// files on the TF card so a failed NVS update cannot overwrite an old capture.
namespace RawSignalFiles {
constexpr size_t PathSize = 48;
bool ensureCard();
fs::FS* locate(int slot, uint64_t id, char (&path)[PathSize]);
bool allocate(uint64_t& id, char (&path)[PathSize]);
bool copyDraft(const char* source, uint64_t& id);
void remove(int slot, uint64_t id);
}
