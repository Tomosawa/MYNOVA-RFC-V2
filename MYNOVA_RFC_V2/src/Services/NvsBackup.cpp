#include "NvsBackup.h"
#include "DataStore.h"
#include "../Util/Log.h"
#include "../Util/MemAlloc.h"
#include <nvs.h>
#include <cstring>

namespace {

constexpr const char* namespaces[] = {
    DataStore::SignalsNamespace, DataStore::SettingsNamespace, DataStore::MqttNamespace, Log::NVS_NS
};
constexpr uint8_t groupCount = sizeof(namespaces) / sizeof(namespaces[0]);

struct Record {
    uint8_t group;
    uint8_t type;
    uint16_t length;
    char key[16];
};
static_assert(sizeof(Record) == 20);

bool selected(uint8_t group, uint8_t sections) {
    return sections & (group == 0 ? NvsBackup::Signals : NvsBackup::Settings);
}

size_t scalarSize(uint8_t type) {
    switch (type) {
        case NVS_TYPE_I8: case NVS_TYPE_U8: return 1;
        case NVS_TYPE_I16: case NVS_TYPE_U16: return 2;
        case NVS_TYPE_I32: case NVS_TYPE_U32: return 4;
        case NVS_TYPE_I64: case NVS_TYPE_U64: return 8;
        default: return 0;
    }
}

esp_err_t getValue(nvs_handle_t handle, const Record& record, uint8_t* out, size_t& size) {
    // Copy through a properly aligned local for scalar NVS APIs.
#define READ_SCALAR(code, suffix, type) \
    case code: { \
        type value{}; \
        auto error = nvs_get_##suffix(handle, record.key, &value); \
        memcpy(out, &value, sizeof(value)); \
        return error; \
    }
    switch (record.type) {
        READ_SCALAR(NVS_TYPE_I8, i8, int8_t)
        READ_SCALAR(NVS_TYPE_U8, u8, uint8_t)
        READ_SCALAR(NVS_TYPE_I16, i16, int16_t)
        READ_SCALAR(NVS_TYPE_U16, u16, uint16_t)
        READ_SCALAR(NVS_TYPE_I32, i32, int32_t)
        READ_SCALAR(NVS_TYPE_U32, u32, uint32_t)
        READ_SCALAR(NVS_TYPE_I64, i64, int64_t)
        READ_SCALAR(NVS_TYPE_U64, u64, uint64_t)
        case NVS_TYPE_STR: return nvs_get_str(handle, record.key, reinterpret_cast<char*>(out), &size);
        case NVS_TYPE_BLOB: return nvs_get_blob(handle, record.key, out, &size);
        default: return ESP_ERR_INVALID_ARG;
    }
#undef READ_SCALAR
}

esp_err_t setValue(nvs_handle_t handle, const Record& record, const uint8_t* bytes) {
#define WRITE_SCALAR(code, suffix, type) \
    case code: { \
        type value; \
        memcpy(&value, bytes, sizeof(value)); \
        return nvs_set_##suffix(handle, record.key, value); \
    }
    switch (record.type) {
        WRITE_SCALAR(NVS_TYPE_I8, i8, int8_t)
        WRITE_SCALAR(NVS_TYPE_U8, u8, uint8_t)
        WRITE_SCALAR(NVS_TYPE_I16, i16, int16_t)
        WRITE_SCALAR(NVS_TYPE_U16, u16, uint16_t)
        WRITE_SCALAR(NVS_TYPE_I32, i32, int32_t)
        WRITE_SCALAR(NVS_TYPE_U32, u32, uint32_t)
        WRITE_SCALAR(NVS_TYPE_I64, i64, int64_t)
        WRITE_SCALAR(NVS_TYPE_U64, u64, uint64_t)
        case NVS_TYPE_STR: return nvs_set_str(handle, record.key, reinterpret_cast<const char*>(bytes));
        case NVS_TYPE_BLOB: return nvs_set_blob(handle, record.key, bytes, record.length);
        default: return ESP_ERR_INVALID_ARG;
    }
#undef WRITE_SCALAR
}

Record readRecord(const uint8_t* bytes) {
    Record record;
    memcpy(&record, bytes, sizeof(record));
    return record;
}

bool writeGroup(uint8_t group, const uint8_t* data, size_t size, bool erase) {
    nvs_handle_t handle;
    // Opening a missing namespace in read-only mode keeps absent settings absent.
    auto error = nvs_open(namespaces[group], NVS_READONLY, &handle);
    if (error == ESP_OK) nvs_close(handle);
    else if (error != ESP_ERR_NVS_NOT_FOUND) return false;
    bool exists = error == ESP_OK;
    for (size_t pos = 0; !exists && pos < size;) {
        Record record = readRecord(data + pos);
        exists = record.group == group;
        pos += sizeof(record) + record.length;
    }
    if (!exists) return true;

    if (nvs_open(namespaces[group], NVS_READWRITE, &handle) != ESP_OK) return false;
    if (erase) error = nvs_erase_all(handle);
    for (size_t pos = 0; error == ESP_OK && pos < size;) {
        Record record = readRecord(data + pos);
        pos += sizeof(record);
        if (record.group == group) error = setValue(handle, record, data + pos);
        pos += record.length;
    }
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error == ESP_OK;
}

} // namespace

NvsBackup::NvsBackup() : data(static_cast<uint8_t*>(anyMalloc(Capacity))) {}
NvsBackup::~NvsBackup() { free(data); }

bool NvsBackup::capture(uint8_t sections) {
    size = 0;
    if (!data) return false;
    for (uint8_t group = 0; group < groupCount; ++group) {
        if (!selected(group, sections)) continue;
        nvs_handle_t handle;
        auto error = nvs_open(namespaces[group], NVS_READONLY, &handle);
        if (error == ESP_ERR_NVS_NOT_FOUND) continue;
        if (error != ESP_OK) return false;

        nvs_iterator_t it = nullptr;
        error = nvs_entry_find_in_handle(handle, NVS_TYPE_ANY, &it);
        while (error == ESP_OK) {
            nvs_entry_info_t info;
            error = nvs_entry_info(it, &info);
            if (error != ESP_OK) break;
            if (Capacity - size < sizeof(Record) + sizeof(uint64_t)) {
                error = ESP_ERR_NO_MEM;
                break;
            }
            Record record{group, static_cast<uint8_t>(info.type), 0, {}};
            strlcpy(record.key, info.key, sizeof(record.key));
            size_t length = scalarSize(record.type);
            if (!length) length = Capacity - size - sizeof(record);
            error = getValue(handle, record, data + size + sizeof(record), length);
            if (error != ESP_OK) break;
            record.length = length;
            memcpy(data + size, &record, sizeof(record));
            size += sizeof(record) + length;
            error = nvs_entry_next(&it);
        }
        nvs_release_iterator(it);
        nvs_close(handle);
        if (error != ESP_ERR_NVS_NOT_FOUND) return false;
    }
    return true;
}

bool NvsBackup::validate(uint8_t sections) const {
    if (!data || size > Capacity) return false;
    for (size_t pos = 0; pos < size;) {
        if (size - pos < sizeof(Record)) return false;
        Record record = readRecord(data + pos);
        const uint8_t* bytes = data + pos + sizeof(record);
        if (record.group >= groupCount || !selected(record.group, sections) ||
            !record.key[0] || !memchr(record.key, 0, sizeof(record.key)) ||
            record.length > size - pos - sizeof(record)) return false;
        const size_t scalar = scalarSize(record.type);
        if (scalar ? record.length != scalar :
            record.type != NVS_TYPE_BLOB &&
            (record.type != NVS_TYPE_STR || !record.length ||
             memchr(bytes, 0, record.length) != bytes + record.length - 1)) return false;

        for (size_t prev = 0; prev < pos;) {
            Record other = readRecord(data + prev);
            if (other.group == record.group && !strcmp(other.key, record.key)) return false;
            prev += sizeof(other) + other.length;
        }
        pos += sizeof(record) + record.length;
    }
    return true;
}

bool NvsBackup::replace(uint8_t sections) const {
    // Erase all selected namespaces first to free their space for the incoming image.
    for (uint8_t group = 0; group < groupCount; ++group) {
        if (selected(group, sections) && !writeGroup(group, data, 0, true)) return false;
    }
    for (uint8_t group = 0; group < groupCount; ++group) {
        if (selected(group, sections) && !writeGroup(group, data, size, false)) return false;
    }
    return true;
}

bool NvsBackup::canReplace(uint8_t sections) const {
    // Missing namespaces consume permanent entries even if their values are rolled back.
    size_t entries[groupCount]{};
    for (size_t pos = 0; pos < size;) {
        Record record = readRecord(data + pos);
        size_t count = 1;
        if (record.type == NVS_TYPE_STR) count += (record.length + 31) / 32;
        if (record.type == NVS_TYPE_BLOB) count += 1 + (record.length + 31) / 32;
        entries[record.group] += count;
        pos += sizeof(record) + record.length;
    }

    nvs_stats_t stats;
    if (nvs_get_stats(nullptr, &stats) != ESP_OK) return false;
    size_t reclaimed = 0;
    size_t required = 0;
    size_t newNamespaces = 0;
    for (uint8_t group = 0; group < groupCount; ++group) {
        if (!selected(group, sections)) continue;
        required += entries[group];
        nvs_handle_t handle;
        auto error = nvs_open(namespaces[group], NVS_READONLY, &handle);
        if (error == ESP_ERR_NVS_NOT_FOUND) {
            if (entries[group]) ++newNamespaces;
            continue;
        }
        if (error != ESP_OK) return false;
        size_t used = 0;
        error = nvs_get_used_entry_count(handle, &used);
        nvs_close(handle);
        if (error != ESP_OK) return false;
        reclaimed += used;
    }
    return newNamespaces <= stats.available_entries &&
           required + newNamespaces <= stats.available_entries + reclaimed;
}

bool NvsBackup::rawSlots(RawSlot* slots) const {
    for (size_t pos = 0; pos < size;) {
        Record record = readRecord(data + pos);
        pos += sizeof(record);
        if (record.group == 0 && (!strncmp(record.key, "rf_", 3) || !strncmp(record.key, "ir_", 3))) {
            const bool rf = record.key[0] == 'r';
            char* end = nullptr;
            long slot = strtol(record.key + 3, &end, 10);
            SignalEntry entry;
            if (*end || slot < 1 || slot > (rf ? RF_SLOT_COUNT : IR_SLOT_COUNT) ||
                record.type != NVS_TYPE_BLOB ||
                !record.length || data[pos] != (rf ? SIG_RF : SIG_IR) ||
                !DataStore::DecodeSignalBlob(data + pos, record.length, entry) ||
                entry.ir.frameCount > IR_MAX_FRAMES) return false;
            if (rf && entry.rf.rawFlag) {
                if (slots[slot - 1].present) return false;
                slots[slot - 1] = {true, entry.rf.rawFileId};
            }
        }
        pos += record.length;
    }
    return true;
}

bool NvsBackup::setRawFileId(int slot, uint64_t fileId) {
    char key[16];
    snprintf(key, sizeof(key), "rf_%d", slot);
    for (size_t pos = 0; pos < size;) {
        Record record = readRecord(data + pos);
        const size_t payload = pos + sizeof(record);
        const size_t next = payload + record.length;
        if (record.group == 0 && !strcmp(key, record.key)) {
            SignalEntry entry;
            if (!DataStore::DecodeSignalBlob(data + payload, record.length, entry) ||
                !entry.rf.rawFlag) return false;
            entry.rf.rawFileId = fileId;
            uint8_t encoded[144]; // RF metadata including the maximum slot name.
            int length = DataStore::EncodeRfSignalBlob(entry, encoded, sizeof(encoded));
            if (length <= 0 || size - record.length + length > Capacity) return false;

            memmove(data + payload + length, data + next, size - next);
            memcpy(data + payload, encoded, length);
            size = size - record.length + length;
            record.length = length;
            memcpy(data + pos, &record, sizeof(record));
            return true;
        }
        pos = next;
    }
    return false;
}
