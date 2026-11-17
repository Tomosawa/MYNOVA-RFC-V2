#pragma once
#include "FS.h"
inline bool testMountOk = true;
struct TestLittleFS {
    bool begin(bool) { return testMountOk; }
    bool exists(const char* path) { return testFiles.count(path); }
    bool remove(const char* path) { return testFiles.erase(path) > 0; }
    bool rename(const char* from, const char* to) {
        if (!exists(from)) return false;
        testFiles[to] = testFiles[from];
        return remove(from);
    }
    File open(const char* path, const char*) {
        auto content = std::make_shared<std::string>();
        testFiles[path] = content;
        return File(content);
    }
};
inline TestLittleFS LittleFS;
