// server/Logger.h
#pragma once

#include <cstdarg>

namespace dp {

enum class LogLevel : int { Debug = 0, Info = 1, Warn = 2, Error = 3 };

/// 极简日志：时间戳 + 级别 + 内容，写 stdout。
/// 服务端为单线程 reactor，加锁只是为了将来引入线程池时不出乱序。
class Logger {
public:
    static Logger& instance();

    void setLevel(LogLevel lv) { level_ = lv; }
    LogLevel level() const { return level_; }

    void write(LogLevel lv, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

    /// 解析 "debug|info|warn|error"，失败返回 false。
    static bool parseLevel(const char* s, LogLevel& out);

private:
    LogLevel level_ = LogLevel::Info;
};

}  // namespace dp

#define LOG_DEBUG(...) ::dp::Logger::instance().write(::dp::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::dp::Logger::instance().write(::dp::LogLevel::Info,  __VA_ARGS__)
#define LOG_WARN(...)  ::dp::Logger::instance().write(::dp::LogLevel::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) ::dp::Logger::instance().write(::dp::LogLevel::Error, __VA_ARGS__)
