/*
 * 文件名:    Logger.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  CLogger 头文件，声明相关类型与接口。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

/* server/CLogger.h */
#pragma once

#include <cstdarg>

namespace dp {

enum class LogLevel_EN : int { Debug = 0, Info = 1, Warn = 2, Error = 3 };

/* 极简日志：时间戳 + 级别 + 内容，写 stdout。 */
/* 服务端为单线程 reactor，加锁只是为了将来引入线程池时不出乱序。 */
class CLogger {
public:
    static CLogger& instance();

    void setLevel(LogLevel_EN lv) { m_stLevel = lv; }
    LogLevel_EN level() const { return m_stLevel; }

    void write(LogLevel_EN lv, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

    /* 解析 "debug|info|warn|error"，失败返回 false。 */
    static bool parseLevel(const char* s, LogLevel_EN& out);

private:
    LogLevel_EN m_stLevel = LogLevel_EN::Info;
};

/* namespace dp */
}

#define LOG_DEBUG(...) ::dp::CLogger::instance().write(::dp::LogLevel_EN::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::dp::CLogger::instance().write(::dp::LogLevel_EN::Info,  __VA_ARGS__)
#define LOG_WARN(...)  ::dp::CLogger::instance().write(::dp::LogLevel_EN::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) ::dp::CLogger::instance().write(::dp::LogLevel_EN::Error, __VA_ARGS__)
