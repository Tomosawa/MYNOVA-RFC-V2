/*
  FlipperFormat.h — Flipper 文本键值文件格式读写器（lib/flipper_format 移植）

  参考：flipperzero-firmware-dev/lib/flipper_format/flipper_format_stream.c

  格式（Flipper .sub / .ir / .nfc 等所有信号文件共用）：
    Filetype: Flipper SubGhz Key File
    Version: 1
    Frequency: 433920000
    Protocol: CAME
    # 注释行
    Key: 00 00 00 00 12 34 56 78

  要点（与 Flipper 原版行为对齐）：
  1. 键值 = "Key: value"，冒号后一个空格；'#' 开头注释；空行忽略；
  2. 数组值（hex / int32 / uint32）写在同一行，空格分隔，不换行
     （flipper_format_stream_write_value_line 单行写完才输出 EOL）；
  3. 读解析大小写敏感（与 Flipper 一致："Bit"/"Key" 与 "name"/"type"）；
  4. Key 的 hex 数组为 8 字节，高字节在前（大端 uint64）。
*/

#ifndef FLIPPER_FORMAT_H
#define FLIPPER_FORMAT_H

#include <Arduino.h>
#include <FS.h>

class FlipperFormat {
public:
    FlipperFormat() = default;
    ~FlipperFormat() { close(); }

    // ── 读模式 ──
    bool openRead(fs::FS& fs, const char* path) {
        close();
        _file = fs.open(path, "r");
        return (bool)_file;
    }

    bool isOpen() const { return (bool)_file; }

    // 读下一对键值（自动跳过空行与 '#' 注释行）。
    // value 是冒号后的整行内容（已 trim）。EOF 返回 false。
    bool next(String& key, String& value) {
        if (!isOpen()) return false;
        while (_file.available()) {
            String line = _file.readStringUntil('\n');
            line.trim();                       // 去掉 \r 与首尾空白
            if (line.length() == 0) continue;
            if (line[0] == '#') continue;       // 注释
            int colon = line.indexOf(':');
            if (colon <= 0) continue;           // 脏行：无 key 或空 key
            key = line.substring(0, colon);
            key.trim();
            value = line.substring(colon + 1);
            value.trim();
            return true;
        }
        return false;
    }

    // ── 写模式 ──
    bool openWrite(fs::FS& fs, const char* path) {
        close();
        _file = fs.open(path, FILE_WRITE);
        return (bool)_file;
    }

    // 文件头：Filetype/Version 两行
    bool writeHeader(const char* filetype, uint32_t version) {
        return writeKV("Filetype", filetype) && writeKV_U32("Version", version);
    }

    bool writeKV(const char* key, const char* value) {
        if (!isOpen()) return false;
        return _file.printf("%s: %s\n", key, value) > 0;
    }

    bool writeKV_U32(const char* key, uint32_t value) {
        if (!isOpen()) return false;
        return _file.printf("%s: %lu\n", key, (unsigned long)value) > 0;
    }

    // 8 字节大端 hex（Flipper .sub 的 Key 格式："00 00 00 00 12 34 56 78"）
    bool writeKV_Hex64(const char* key, uint64_t value) {
        if (!isOpen()) return false;
        int n = _file.printf("%s: ", key);
        for (int8_t i = 7; i >= 0; i--) {
            n += _file.printf("%02X", (uint8_t)(value >> (i * 8)));
            if (i > 0) _file.print(' ');
        }
        _file.print('\n');
        return n > 0;
    }

    // uint32 数组单行："RAW_Data: -320 640 -640 ..."
    bool writeKV_U32Array(const char* key, const uint32_t* values, uint16_t count) {
        if (!isOpen()) return false;
        int n = _file.printf("%s: ", key);
        for (uint16_t i = 0; i < count; i++) {
            n += _file.printf("%s%lu", (i == 0) ? "" : " ", (unsigned long)values[i]);
        }
        _file.print('\n');
        return n > 0;
    }

    // int32 数组单行（RAW_Data 的正负号即电平）："RAW_Data: -320 640 ..."
    bool writeKV_I32Array(const char* key, const int32_t* values, uint16_t count) {
        if (!isOpen()) return false;
        int n = _file.printf("%s: ", key);
        for (uint16_t i = 0; i < count; i++) {
            n += _file.printf("%s%d", (i == 0) ? "" : " ", (int)values[i]);
        }
        _file.print('\n');
        return n > 0;
    }

    void close() { if ((bool)_file) { _file.close(); } }

private:
    File _file;
};

#endif
