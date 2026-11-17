/*
 * Copyright (c) 2026 Tomosawa
 * https://github.com/Tomosawa/
 * All rights reserved
 */
#include "BackupManager.h"
#include "DataStore.h"
#include "NvsBackup.h"
#include "RawSignalFiles.h"
#include "SystemSetting.h"
#include "../Drivers/AudioPlayer.h"
#include "../Lib/RawCaptureSession.h"
#include "../Lib/RawSubGhzReader.h"
#include <LittleFS.h>
#include <esp_rom_crc.h>
#include <algorithm>
#include <mutex>
#include <time.h>

extern DataStore dataStore;
extern SystemSetting systemSetting;
extern AudioPlayer audioPlayer;

namespace {

constexpr char BackupDir[] = "/backup";
constexpr char Prefix[] = "MYNOVA_BK_";
constexpr char Extension[] = ".mynova";
constexpr char WorkDir[] = "/.restore";
constexpr char Journal[] = "/.restore/pending";
constexpr char JournalTemp[] = "/.restore/journal.tmp";
constexpr char ArchiveMagic[] = "MYNOVAB1";
constexpr char JournalMagic[] = "MYNOVAJ2";
constexpr char LegacyJournalMagic[] = "MYNOVAJ1";
constexpr uint32_t RawSizeLimit = 2 * 1024 * 1024; // Larger than the entire LittleFS partition.
std::mutex backupMutex;

// Paths from older rollback journals. Current restore never moves a live RAW file.
struct LegacyRawPaths {
    char live[32], incoming[32], original[32];
    explicit LegacyRawPaths(int slot) {
        RawCaptureSession::sidecarPath(slot, live, sizeof(live));
        snprintf(incoming, sizeof(incoming), "%s/new%d", WorkDir, slot);
        snprintf(original, sizeof(original), "%s/old%d", WorkDir, slot);
    }
};

// The ESP32 format stores little-endian scalars. CRC covers every byte up to the footer.
class CheckedFile {
public:
    explicit CheckedFile(File& file) : file(file) {}

    bool read(void* out, size_t len) {
        auto* bytes = static_cast<uint8_t*>(out);
        if (len && file.read(bytes, len) != len) return false;
        crc = esp_rom_crc32_le(crc, bytes, len);
        return true;
    }

    bool write(const void* in, size_t len) {
        const auto* bytes = static_cast<const uint8_t*>(in);
        if (len && file.write(bytes, len) != len) return false;
        crc = esp_rom_crc32_le(crc, bytes, len);
        return true;
    }

    bool finishWrite() { return file.write(reinterpret_cast<const uint8_t*>(&crc), sizeof(crc)) == sizeof(crc); }
    bool finishRead() {
        uint32_t expected;
        return file.read(reinterpret_cast<uint8_t*>(&expected), sizeof(expected)) == sizeof(expected) &&
               expected == crc && file.position() == file.size();
    }

private:
    File& file;
    uint32_t crc = 0;
};

bool validName(const String& name) {
    if (name.length() > 70 || !name.startsWith(Prefix) || !name.endsWith(Extension)) return false;
    for (size_t i = sizeof(Prefix) - 1; i < name.length() - sizeof(Extension) + 1; ++i) {
        char c = name[i];
        if ((c < '0' || c > '9') && c != '_' && c != '-') return false;
    }
    return true;
}

String backupPath(const String& name) { return String(BackupDir) + "/" + name; }

String makeName(fs::FS& fs) {
    time_t now = time(nullptr);
    String stamp;
    if (now > 1700000000) {
        tm local;
        localtime_r(&now, &local);
        char buf[24];
        strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &local);
        stamp = buf;
    } else {
        stamp = String(millis());
    }
    String base = String(Prefix) + stamp;
    String name = base + Extension;
    for (unsigned i = 1; fs.exists(backupPath(name)); ++i) {
        name = base + "_" + i + Extension;
    }
    return name;
}

bool removeIfPresent(fs::FS& fs, const char* path) {
    return !fs.exists(path) || fs.remove(path);
}

bool cleanupWorkDir() {
    // A pending journal is needed until either commit or rollback has completed.
    if (LittleFS.exists(Journal)) return false;
    if (!LittleFS.exists(WorkDir)) return true;
    bool ok = removeIfPresent(LittleFS, JournalTemp);
    for (int slot = 1; slot <= RF_SLOT_COUNT; ++slot) {
        LegacyRawPaths paths(slot);
        ok = removeIfPresent(LittleFS, paths.incoming) && ok;
        ok = removeIfPresent(LittleFS, paths.original) && ok;
    }
    return LittleFS.rmdir(WorkDir) && ok;
}

bool readImage(CheckedFile& file, NvsBackup& image, uint8_t sections) {
    uint32_t length;
    if (!image.data || !file.read(&length, sizeof(length)) || length > NvsBackup::Capacity) return false;
    image.size = length;
    return file.read(image.data, image.size) && image.validate(sections);
}

bool writeImage(CheckedFile& file, const NvsBackup& image) {
    const uint32_t length = image.size;
    return file.write(&length, sizeof(length)) && file.write(image.data, image.size);
}

bool rawFileValid(fs::FS& fs, const char* path) {
    RawSubGhzReader reader;
    if (!reader.open(fs, path) ||
        reader.frequencyHz < 300000000 || reader.frequencyHz > 460000000 ||
        strcmp(reader.preset, RfPreset(RfPresetIdFromName(reader.preset)).flipperName)) return false;
    int32_t pulse;
    int status;
    while ((status = reader.next(pulse)) > 0) {
        if ((reader.count & 0x7ff) == 0) delay(1);
    }
    return status == 0 && reader.count > 0;
}

bool exportRaw(CheckedFile& archive, int slot, uint64_t fileId) {
    char path[RawSignalFiles::PathSize];
    auto* fs = RawSignalFiles::locate(slot, fileId, path);
    if (!fs || !rawFileValid(*fs, path)) return false;
    File input = fs->open(path, FILE_READ);
    const uint32_t bytes = input.size();
    const uint32_t id = slot;
    if (!input || !bytes || bytes > RawSizeLimit ||
        !archive.write(&id, sizeof(id)) || !archive.write(&bytes, sizeof(bytes))) return false;

    uint8_t buffer[1024];
    for (uint32_t offset = 0; offset < bytes;) {
        size_t length = std::min<size_t>(bytes - offset, sizeof(buffer));
        if (input.read(buffer, length) != length || !archive.write(buffer, length)) return false;
        offset += length;
        if ((offset & 0x3fff) == 0) delay(1);
    }
    return true;
}

bool readRaw(CheckedFile& archive, const char* stagePath, uint32_t bytes) {
    auto& fs = audioPlayer.getFS();
    File staged;
    if (stagePath) {
        staged = fs.open(stagePath, FILE_WRITE);
        if (!staged) return false;
    }
    uint8_t buffer[1024];
    uint32_t crc = 0;
    for (uint32_t offset = 0; offset < bytes;) {
        size_t length = std::min<size_t>(bytes - offset, sizeof(buffer));
        if (!archive.read(buffer, length)) return false;
        if (stagePath) {
            if (staged.write(buffer, length) != length) return false;
            crc = esp_rom_crc32_le(crc, buffer, length);
        }
        offset += length;
        if ((offset & 0x3fff) == 0) delay(1);
    }
    if (!stagePath) return true;

    // Read the staged bytes back before touching the live slot.
    staged.close();
    staged = fs.open(stagePath, FILE_READ);
    if (!staged || staged.size() != bytes) return false;
    uint32_t actual = 0;
    for (uint32_t offset = 0; offset < bytes;) {
        size_t length = std::min<size_t>(bytes - offset, sizeof(buffer));
        if (staged.read(buffer, length) != length) return false;
        actual = esp_rom_crc32_le(actual, buffer, length);
        offset += length;
        if ((offset & 0x3fff) == 0) delay(1);
    }
    staged.close();
    return actual == crc && rawFileValid(fs, stagePath);
}

bool readArchive(File& file, NvsBackup& image, uint64_t* stagedIds = nullptr) {
    CheckedFile archive(file);
    char magic[sizeof(ArchiveMagic)];
    if (!file || !archive.read(magic, sizeof(magic)) || memcmp(magic, ArchiveMagic, sizeof(magic)) ||
        !readImage(archive, image, NvsBackup::All)) return false;

    NvsBackup::RawSlot rawSlots[RF_SLOT_COUNT]{};
    if (!image.rawSlots(rawSlots)) return false;
    for (uint32_t slot = 1; slot <= RF_SLOT_COUNT; ++slot) {
        if (!rawSlots[slot - 1].present) continue;
        uint32_t id, bytes;
        if (!archive.read(&id, sizeof(id)) || id != slot ||
            !archive.read(&bytes, sizeof(bytes)) || !bytes || bytes > RawSizeLimit) return false;
        char path[RawSignalFiles::PathSize];
        if (stagedIds && !RawSignalFiles::allocate(stagedIds[slot - 1], path)) return false;
        if (!readRaw(archive, stagedIds ? path : nullptr, bytes)) return false;
    }
    if (!archive.finishRead()) return false;
    if (stagedIds) {
        for (int slot = 1; slot <= RF_SLOT_COUNT; ++slot) {
            if (stagedIds[slot - 1] && !image.setRawFileId(slot, stagedIds[slot - 1])) return false;
        }
    }
    return true;
}

struct StagedRaw {
    uint64_t ids[RF_SLOT_COUNT]{};
    bool committed = false;

    ~StagedRaw() {
        if (committed) return;
        for (int slot = 1; slot <= RF_SLOT_COUNT; ++slot) {
            if (ids[slot - 1]) RawSignalFiles::remove(slot, ids[slot - 1]);
        }
    }
};

struct Rollback {
    NvsBackup image;
    uint8_t sections;
    bool legacy = false;
    uint8_t hasOriginal[RF_SLOT_COUNT]{};

    explicit Rollback(uint8_t sections = 0) : sections(sections) {}

    bool read(const char* path) {
        File file = LittleFS.open(path, FILE_READ);
        CheckedFile journal(file);
        char magic[sizeof(JournalMagic)];
        if (!file || !journal.read(magic, sizeof(magic))) return false;
        legacy = !memcmp(magic, LegacyJournalMagic, sizeof(magic));
        if (!legacy && memcmp(magic, JournalMagic, sizeof(magic))) return false;
        return journal.read(&sections, sizeof(sections)) && sections && !(sections & ~NvsBackup::All) &&
               journal.read(hasOriginal, sizeof(hasOriginal)) && readImage(journal, image, sections) &&
               journal.finishRead();
    }

    bool prepare() {
        if (!image.capture(sections)) return false;
        File file = LittleFS.open(JournalTemp, FILE_WRITE);
        CheckedFile journal(file);
        if (!file || !journal.write(JournalMagic, sizeof(JournalMagic)) ||
            !journal.write(&sections, sizeof(sections)) ||
            !journal.write(hasOriginal, sizeof(hasOriginal)) ||
            !writeImage(journal, image) || !journal.finishWrite()) return false;
        file.close();
        return read(JournalTemp) && LittleFS.rename(JournalTemp, Journal);
    }

    bool restoreOriginal() const {
        if (!image.replace(sections)) return false;
        if (legacy && (sections & NvsBackup::Signals)) {
            for (int slot = 1; slot <= RF_SLOT_COUNT; ++slot) {
                LegacyRawPaths paths(slot);
                if (!hasOriginal[slot - 1]) {
                    if (!removeIfPresent(LittleFS, paths.live)) return false;
                } else if (LittleFS.exists(paths.original)) {
                    // If power is lost between these operations, original still exists and recovery retries.
                    if (!removeIfPresent(LittleFS, paths.live) ||
                        !LittleFS.rename(paths.original, paths.live)) return false;
                } else if (!LittleFS.exists(paths.live)) {
                    return false;
                }
            }
        }
        return LittleFS.remove(Journal);
    }
};

} // namespace

bool BackupManager::ensureCardReady() {
    return RawSignalFiles::ensureCard();
}

std::vector<String> BackupManager::listBackups() {
    std::vector<String> files;
    if (!ensureCardReady()) return files;
    File dir = audioPlayer.getFS().open(BackupDir);
    if (!dir || !dir.isDirectory()) return files;
    for (File file = dir.openNextFile(); file; file = dir.openNextFile()) {
        const String name = file.name();
        if (!file.isDirectory() && validName(name)) files.push_back(name);
    }
    std::sort(files.begin(), files.end(), [](const String& a, const String& b) { return a > b; });
    return files;
}

bool BackupManager::exportBackup(String& outPath, String& outError) {
    std::unique_lock<std::mutex> operation(backupMutex, std::try_to_lock);
    if (!operation.owns_lock()) { outError = "备份管理忙"; return false; }
    if (!ensureCardReady()) { outError = "TF卡不可用"; return false; }
    fs::FS& fs = audioPlayer.getFS();
    if (!fs.exists(BackupDir) && !fs.mkdir(BackupDir)) {
        outError = "无法创建备份目录";
        return false;
    }

    NvsBackup image;
    NvsBackup::RawSlot rawSlots[RF_SLOT_COUNT]{};
    String name = makeName(fs);
    String path = backupPath(name);
    String temp = path + ".tmp";
    bool ok;
    {
        DataStore::StorageGuard storage(dataStore);
        if (!storage || !image.capture(NvsBackup::All) || !image.rawSlots(rawSlots)) {
            outError = "无法读取NVS数据";
            return false;
        }

        File file = fs.open(temp, FILE_WRITE);
        CheckedFile archive(file);
        ok = file && archive.write(ArchiveMagic, sizeof(ArchiveMagic)) && writeImage(archive, image);
        for (int slot = 1; ok && slot <= RF_SLOT_COUNT; ++slot) {
            if (rawSlots[slot - 1].present) ok = exportRaw(archive, slot, rawSlots[slot - 1].fileId);
        }
        ok = ok && archive.finishWrite();
    }
    if (ok) {
        File file = fs.open(temp, FILE_READ);
        ok = readArchive(file, image);
    }
    if (!ok || !fs.rename(temp, path)) {
        fs.remove(temp);
        outError = "备份写入或校验失败";
        return false;
    }
    outPath = name;
    return true;
}

bool BackupManager::importBackup(const String& fileName, bool restoreSignals,
                                 bool restoreSystem, String& outMessage) {
    std::unique_lock<std::mutex> operation(backupMutex, std::try_to_lock);
    if (!operation.owns_lock()) { outMessage = "备份管理忙"; return false; }
    uint8_t sections = (restoreSignals ? NvsBackup::Signals : 0) |
                       (restoreSystem ? NvsBackup::Settings : 0);
    if (!sections || !validName(fileName)) { outMessage = "恢复选项或文件名无效"; return false; }
    if (!ensureCardReady()) { outMessage = "TF卡不可用"; return false; }
    if (!RawCaptureSession::ensureMounted() || !cleanupWorkDir() || !LittleFS.mkdir(WorkDir)) {
        outMessage = "恢复暂存区不可用";
        return false;
    }

    NvsBackup incoming;
    StagedRaw staged;
    File file = audioPlayer.getFS().open(backupPath(fileName), FILE_READ);
    bool valid = readArchive(file, incoming, restoreSignals ? staged.ids : nullptr);
    file.close();
    if (!valid) {
        cleanupWorkDir();
        outMessage = "校验或暂存失败，原数据已保留";
        return false;
    }

    {
        DataStore::StorageGuard storage(dataStore);
        if (!storage || !incoming.canReplace(sections)) {
            cleanupWorkDir();
            outMessage = "NVS忙或空间不足，原数据已保留";
            return false;
        }
        Rollback rollback(sections);
        if (!rollback.prepare()) {
            cleanupWorkDir();
            outMessage = "无法保存回滚数据，原数据已保留";
            return false;
        }
        if (!incoming.replace(sections) || !LittleFS.remove(Journal)) {
            if (!rollback.restoreOriginal()) {
                Serial.println("Backup restore interrupted; restarting to replay rollback journal");
                ESP.restart();
            }
            cleanupWorkDir();
            outMessage = "恢复失败，已回滚";
            return false;
        }
        staged.committed = true;
        NvsBackup::RawSlot previous[RF_SLOT_COUNT]{};
        if (restoreSignals && rollback.image.rawSlots(previous)) {
            for (int slot = 1; slot <= RF_SLOT_COUNT; ++slot) {
                if (previous[slot - 1].present) RawSignalFiles::remove(slot, previous[slot - 1].fileId);
            }
        }
        dataStore.NotifyStorageRestored();
        if (restoreSystem) systemSetting.reloadRestoredConfig();
    }

    cleanupWorkDir();
    outMessage = "恢复完成";
    return true;
}

bool BackupManager::deleteBackup(const String& fileName) {
    std::unique_lock<std::mutex> operation(backupMutex, std::try_to_lock);
    return operation.owns_lock() && validName(fileName) && ensureCardReady() &&
           audioPlayer.getFS().remove(backupPath(fileName));
}

bool BackupManager::recoverInterruptedRestore() {
    if (!LittleFS.begin(false) || !LittleFS.exists(WorkDir)) return true;
    if (LittleFS.exists(Journal)) {
        Rollback rollback;
        if (!rollback.read(Journal) || !rollback.restoreOriginal()) return false;
    }
    cleanupWorkDir();
    return true;
}
