#include "RawSignalFiles.h"
#include "../Drivers/AudioPlayer.h"
#include "../Lib/RawCaptureSession.h"
#include <LittleFS.h>
#include <esp_random.h>
#include <esp_rom_crc.h>
#include <algorithm>

extern AudioPlayer audioPlayer;

namespace {
constexpr char Directory[] = "/MYNOVA_RAW";

void cardPath(uint64_t id, char (&path)[RawSignalFiles::PathSize]) {
    snprintf(path, sizeof(path), "%s/%016llX.sub", Directory, (unsigned long long)id);
}
}

bool RawSignalFiles::ensureCard() {
    if (!audioPlayer.isCardReady()) audioPlayer.init();
    return audioPlayer.isCardReady();
}

fs::FS* RawSignalFiles::locate(int slot, uint64_t id, char (&path)[PathSize]) {
    if (id) {
        if (!ensureCard()) return nullptr;
        cardPath(id, path);
        return &audioPlayer.getFS();
    }
    if (!RawCaptureSession::ensureMounted()) return nullptr;
    RawCaptureSession::sidecarPath(slot, path, sizeof(path));
    return &LittleFS;
}

bool RawSignalFiles::allocate(uint64_t& id, char (&path)[PathSize]) {
    if (!ensureCard()) return false;
    auto& fs = audioPlayer.getFS();
    if (!fs.exists(Directory) && !fs.mkdir(Directory)) return false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        uint64_t candidate;
        esp_fill_random(&candidate, sizeof(candidate));
        if (!candidate) continue;
        cardPath(candidate, path);
        if (!fs.exists(path)) {
            id = candidate;
            return true;
        }
    }
    return false;
}

bool RawSignalFiles::copyDraft(const char* source, uint64_t& id) {
    if (!RawCaptureSession::ensureMounted()) return false;
    File input = LittleFS.open(source, FILE_READ);
    if (!input || !input.size()) return false;
    char path[PathSize];
    if (!allocate(id, path)) return false;
    auto& fs = audioPlayer.getFS();

    File output = fs.open(path, FILE_WRITE);
    size_t bytes = input.size();
    uint8_t buffer[1024];
    uint32_t crc = 0;
    bool ok = bool(output);
    for (size_t offset = 0; ok && offset < bytes;) {
        size_t chunk = std::min(bytes - offset, sizeof(buffer));
        ok = input.read(buffer, chunk) == chunk && output.write(buffer, chunk) == chunk;
        if (!ok) break;
        crc = esp_rom_crc32_le(crc, buffer, chunk);
        offset += chunk;
        if ((offset & 0x3fff) == 0) delay(1);
    }
    output.close();
    input.close();
    if (ok) {
        File check = fs.open(path, FILE_READ);
        ok = check && check.size() == bytes;
        uint32_t actual = 0;
        for (size_t offset = 0; ok && offset < bytes;) {
            size_t chunk = std::min(bytes - offset, sizeof(buffer));
            ok = check.read(buffer, chunk) == chunk;
            if (!ok) break;
            actual = esp_rom_crc32_le(actual, buffer, chunk);
            offset += chunk;
            if ((offset & 0x3fff) == 0) delay(1);
        }
        ok = ok && actual == crc;
    }
    if (!ok) {
        fs.remove(path);
        id = 0;
    }
    return ok;
}

void RawSignalFiles::remove(int slot, uint64_t id) {
    char path[PathSize];
    if (auto* fs = locate(slot, id, path)) {
        if (fs->exists(path)) fs->remove(path);
    }
}
