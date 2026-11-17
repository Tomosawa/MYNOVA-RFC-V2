#pragma once
#include <Arduino.h>
#include <cassert>
#include <map>
#include <memory>
#include <string>
#include <cstdarg>
#include "../host_string.h"
#define FILE_WRITE "w"
inline std::map<std::string, std::shared_ptr<std::string>> testFiles;
inline long testWriteBudget = -1;
class File {
    std::shared_ptr<std::string> content;
    size_t position=0;
public:
    File() = default;
    explicit File(std::shared_ptr<std::string> c): content(c) {}
    explicit operator bool() const { return bool(content); }
    size_t write(const uint8_t* data, size_t len) {
        if (!content) return 0;
        if (testWriteBudget >= 0) {
            len = std::min(len, (size_t)testWriteBudget);
            testWriteBudget -= len;
        }
        content->append(reinterpret_cast<const char*>(data), len);
        return len;
    }
    size_t print(char c) {return write((const uint8_t*)&c,1);}
    size_t printf(const char* format,...) {
        char buffer[256]; va_list args; va_start(args,format);
        int n=vsnprintf(buffer,sizeof(buffer),format,args); va_end(args);
        assert(n>=0 && n<(int)sizeof(buffer));
        return write((const uint8_t*)buffer,n);
    }
    int available() const {return content && position<content->size();}
    String readStringUntil(char delimiter) {
        auto end=content->find(delimiter,position);
        if (end==std::string::npos) end=content->size();
        String value=content->substr(position,end-position);position=end+1;return value;
    }
    void flush() {}
    void close() { content.reset(); }
};
namespace fs {
struct FS {
    File open(const char* path,const char* mode) {
        if (strcmp(mode,"r")==0) {
            auto found=testFiles.find(path);
            return found==testFiles.end() ? File{} : File(found->second);
        }
        auto data=std::make_shared<std::string>();testFiles[path]=data;return File(data);
    }
};
}
