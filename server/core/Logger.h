/**
 * @file       Logger.h
 * @brief      极简日志：时间戳 + 级别 + 内容，写 stdout。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#pragma once

#include <cstdarg>

namespace dp {

/**
 * @brief   日志级别，数值越大越严重。
 * @author  JaTin
 */
enum class LogLevel_EN : int {
    /**
     * @brief  逐条请求级别的细节，默认不输出
     */
    Debug = 0,
    /**
     * @brief  生命周期事件（连接、进房、服务注册）
     */
    Info  = 1,
    /**
     * @brief  可恢复的异常（连接被拒、epoll_ctl 失败）
     */
    Warn  = 2,
    /**
     * @brief  需要关注的失败
     */
    Error = 3,
};

/**
 * @brief   进程级日志器（单例）。
 * @details 用函数内静态对象实现单例，C++11 起保证线程安全的一次性初始化。 输出格式：`[时:分:秒.毫秒] [级别] 内容`。
 * @author  JaTin
 */
class CLogger {
public:
    /**
     * @brief      获取全局唯一实例。
     * @note       本函数无输入参数。
     * @return     单例引用。
     * @author     JaTin
     */
    static CLogger& instance();

    /**
     * @brief  设置最低输出级别，低于该级别的日志被丢弃。
     */
    void setLevel(LogLevel_EN lv) { m_stLevel = lv; }

    /**
     * @brief  当前最低输出级别。
     */
    LogLevel_EN level() const { return m_stLevel; }

    /**
     * @brief      写一条日志。
     * @details    @param ... 格式参数。 低于当前级别的调用会直接返回，格式化开销被省掉。
     * @param[in] lv             级别；类型 LogLevel_EN。
     * @param[in] fmt            printf 风格格式串；类型 const char*。
     * @param[in] printf         输入参数；类型 ...) __attribute__((format(。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void write(LogLevel_EN lv, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

    /**
     * @brief      解析级别名。
     * @param[in] s              字符串，取值 `debug|info|warn|error`；类型 const char*。
     * @param[out] out            解析成功时写入级别；类型 LogLevel_EN&。
     * @return     true  解析成功；false 字符串不匹配任何级别，`out` 不被修改。
     * @author     JaTin
     */
    static bool parseLevel(const char* s, LogLevel_EN& out);

private:
    CLogger() = default;

    /**
     * @brief  最低输出级别
     * @note   类型 LogLevel_EN；类/结构体成员变量。
     */
    LogLevel_EN m_stLevel = LogLevel_EN::Info;
};

/**
 * @brief  namespace dp。
 */
}

/**
 * @brief  写 Debug 级别日志。
 */
#define LOG_DEBUG(...) ::dp::CLogger::instance().write(::dp::LogLevel_EN::Debug, __VA_ARGS__)
/**
 * @brief  写 Info 级别日志。
 */
#define LOG_INFO(...)  ::dp::CLogger::instance().write(::dp::LogLevel_EN::Info,  __VA_ARGS__)
/**
 * @brief  写 Warn 级别日志。
 */
#define LOG_WARN(...)  ::dp::CLogger::instance().write(::dp::LogLevel_EN::Warn,  __VA_ARGS__)
/**
 * @brief  写 Error 级别日志。
 */
#define LOG_ERROR(...) ::dp::CLogger::instance().write(::dp::LogLevel_EN::Error, __VA_ARGS__)
