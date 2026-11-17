/*
* Copyright (c) 2026 Tomosawa
* https://github.com/Tomosawa/
* All rights reserved
*/

// Log.h —— 分级日志系统（规划 §六）
// 用法: 每个 .cpp 顶部 #define LOG_TAG "XXX" 后包含本头文件,
//       然后使用 LOG_E/LOG_W/LOG_I/LOG_D。
// 开关: 编译期 build_opt.h 里 -DLOG_COMPILE_MAX=N (0~4) 裁剪;
//       运行期 Log::setLevel() / 串口命令 "LOG:N" / Web接口 /api/loglevel。
#ifndef UTIL_LOG_H
#define UTIL_LOG_H

#include <Arduino.h>
#include <stdarg.h>
#include <stdint.h>
#include <atomic>

enum class LogLevel : uint8_t {
    NONE  = 0,
    ERROR = 1,
    WARN  = 2,
    INFO  = 3,
    DEBUG = 4
};

// 编译期最大级别(未定义时全编入, 由 build_opt.h 或 -D 控制)
#ifndef LOG_COMPILE_MAX
#define LOG_COMPILE_MAX 4
#endif

class Log {
public:
    // 初始化: 从 NVS 恢复运行级别(默认 INFO), 创建输出互斥
    static void begin();
    static void reloadLevel();
    // 运行期切换级别(persist=true 时写入 NVS, 重启保持)
    static void setLevel(LogLevel lv, bool persist = true);
    static LogLevel getLevel();
    static const char* levelName(LogLevel lv);

    // 核心输出: [E][TAG] fmt...(自动补换行, 线程安全)
    static void output(const char* tag, LogLevel lv, const char* fmt, ...);

    // NVS 命名空间与键(供 WebService 串口命令等直接引用)
    static constexpr const char* NVS_NS  = "logcfg";
    static constexpr const char* NVS_KEY = "lvl";

private:
    static void vOutput(const char* tag, LogLevel lv, const char* fmt, va_list ap);
    static std::atomic<LogLevel> s_level;
    static void* s_mutex;  // SemaphoreHandle_t, 用 void* 避免头文件拖入 FreeRTOS
};

// ---- 使用宏(未定义 LOG_TAG 的编译单元默认 "APP") ----
#ifndef LOG_TAG
#define LOG_TAG "APP"
#endif

#if LOG_COMPILE_MAX >= 4
#define LOG_D(...) Log::output(LOG_TAG, LogLevel::DEBUG, __VA_ARGS__)
#else
#define LOG_D(...) do {} while (0)
#endif
#if LOG_COMPILE_MAX >= 3
#define LOG_I(...) Log::output(LOG_TAG, LogLevel::INFO, __VA_ARGS__)
#else
#define LOG_I(...) do {} while (0)
#endif
#if LOG_COMPILE_MAX >= 2
#define LOG_W(...) Log::output(LOG_TAG, LogLevel::WARN, __VA_ARGS__)
#else
#define LOG_W(...) do {} while (0)
#endif
#if LOG_COMPILE_MAX >= 1
#define LOG_E(...) Log::output(LOG_TAG, LogLevel::ERROR, __VA_ARGS__)
#else
#define LOG_E(...) do {} while (0)
#endif

#endif // UTIL_LOG_H
