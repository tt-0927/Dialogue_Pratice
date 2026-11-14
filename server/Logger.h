/**
 * @file       Logger.h
 * @brief      CLogger 头文件，声明相关类型与接口。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

/**
 * @brief  server/CLogger.h。
 */
#pragma once

#include <cstdarg>

namespace dp {

enum class LogLevel_EN : int { Debug = 0, Info = 1, Warn = 2, Error = 3 };

/**
 * @brief  极简日志：时间戳 + 级别 + 内容，写 stdout。
 * @details 服务端为单线程 reactor，加锁只是为了将来引入线程池时不出乱序。
 */
class CLogger {
public:
    /**
     * @brief      获取本类的进程内单例对象。
     * @note       本函数无输入参数。
     * @return     返回 CLogger& 类型的对象引用或指针，调用方按接口约定使用。
     * @author     JaTin
     */
    static CLogger& instance();

    void setLevel(LogLevel_EN lv) { m_stLevel = lv; }
    LogLevel_EN level() const { return m_stLevel; }

    /**
     * @brief      按日志级别格式化并输出一条日志。
     * @param[in] lv             输入参数；类型 LogLevel_EN。
     * @param[in] fmt            格式化字符串；类型 const char*。
     * @param[in] printf         输入参数；类型 ...) __attribute__((format(。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void write(LogLevel_EN lv, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

    /**
     * @brief      解析 "debug|info|warn|error"，失败返回 false。
     * @param[in] s              目标会话对象；类型 const char*。
     * @param[out] out            输入参数；类型 LogLevel_EN&。
     * @return     成功返回 true；参数非法或执行失败时返回 false。
     * @author     JaTin
     */
    static bool parseLevel(const char* s, LogLevel_EN& out);

private:
    /**
     * @brief  日志级别。 初值 = LogLevel_EN::Info。
     * @note   类型 LogLevel_EN；类/结构体成员变量。
     */
    LogLevel_EN m_stLevel = LogLevel_EN::Info;
};

/**
 * @brief  namespace dp。
 */
}

#define LOG_DEBUG(...) ::dp::CLogger::instance().write(::dp::LogLevel_EN::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::dp::CLogger::instance().write(::dp::LogLevel_EN::Info,  __VA_ARGS__)
#define LOG_WARN(...)  ::dp::CLogger::instance().write(::dp::LogLevel_EN::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) ::dp::CLogger::instance().write(::dp::LogLevel_EN::Error, __VA_ARGS__)
