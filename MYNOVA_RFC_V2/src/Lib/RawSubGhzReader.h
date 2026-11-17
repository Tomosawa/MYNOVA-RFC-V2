#pragma once
#include "FlipperFormat.h"
#include <errno.h>
#include <limits.h>

// Flipper式逐行预读，不把整个文件装入固定数组。仅在任务上下文读取文件。
class RawSubGhzReader {
public:
    uint32_t frequencyHz = 0;
    char preset[48] = {};
    uint32_t count = 0;
    bool open(fs::FS& fs, const char* path) {
        frequencyHz = count = 0;
        preset[0] = 0;
        offset = 0;
        bool filetype = false, raw = false;
        if (!format.openRead(fs, path)) return false;
        while (format.next(key, value)) {
            if (key == "Filetype") filetype = value == "Flipper SubGhz RAW File";
            else if (key == "Frequency") frequencyHz = strtoul(value.c_str(), nullptr, 10);
            else if (key == "Preset") {
                if (value.length() >= sizeof(preset)) return false;
                strcpy(preset, value.c_str());
            } else if (key == "Protocol") raw = value == "RAW";
            else if (key == "RAW_Data") return filetype && raw && frequencyHz && preset[0];
        }
        return false;
    }
    // 1=样本；0=EOF；-1=损坏。EOF和错误必须区分，不能把坏文件当成发送完成。
    int next(int32_t& pulse) {
        for (;;) {
            const char* token = value.c_str() + offset;
            while (*token == ' ' || *token == ',' || *token == '\t') ++token;
            if (*token) {
                char* end = nullptr;
                errno = 0;
                long long v = strtoll(token, &end, 10);
                if (end == token || errno == ERANGE || v < INT32_MIN || v > INT32_MAX ||
                    (*end && *end != ' ' && *end != ',' && *end != '\t')) return -1;
                offset = (size_t)(end - value.c_str());
                pulse = (int32_t)v;
                ++count;
                return 1;
            }
            if (!format.next(key, value)) return 0;
            if (key != "RAW_Data") return -1;
            offset = 0;
        }
    }
    static int read(void* context, int32_t& pulse) {
        return static_cast<RawSubGhzReader*>(context)->next(pulse);
    }
private:
    FlipperFormat format;
    String key, value;
    size_t offset = 0;
};
