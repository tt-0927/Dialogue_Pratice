/**
 * @file  Logger.h
 * @brief 极简日志：时间戳 + 级别 + 内容，写 stdout。
 * @details 服务端为单线程 reactor，加锁只是为了将来引入线程池时不出乱序。
 *          日志级别通过命令行 `--log-level` 调整，默认 Info。
 */

#pragma once

#include <cstdarg>

namespace dp {

/**
 * @brief 日志级别，数值越大越严重。
 */
enum class LogLevel : int {
    Debug = 0,   ///< 逐条请求级别的细节，默认不输出
    Info  = 1,   ///< 生命周期事件（连接、进房、服务注册）
    Warn  = 2,   ///< 可恢复的异常（连接被拒、epoll_ctl 失败）
    Error = 3,   ///< 需要关注的失败
};

/**
 * @brief 进程级日志器（单例）。
 *
 * @details 用函数内静态对象实现单例，C++11 起保证线程安全的一次性初始化。
 *          输出格式：`[时:分:秒.毫秒] [级别] 内容`。
 */
class Logger {
public:
    /**
     * @brief 获取全局唯一实例。
     * @retval 单例引用。
     */
    static Logger& instance();

    /// @brief 设置最低输出级别，低于该级别的日志被丢弃。
    void setLevel(LogLevel lv) { level_ = lv; }

    /// @brief 当前最低输出级别。
    LogLevel level() const { return level_; }

    /**
     * @brief 写一条日志。
     * @param lv  级别。
     * @param fmt printf 风格格式串。
     * @param ... 格式参数。
     * @details 低于当前级别的调用会直接返回，格式化开销被省掉。
     */
    void write(LogLevel lv, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

    /**
     * @brief 解析级别名。
     * @param s   字符串，取值 `debug|info|warn|error`。
     * @param out 解析成功时写入级别。
     * @retval true  解析成功。
     * @retval false 字符串不匹配任何级别，`out` 不被修改。
     */
    static bool parseLevel(const char* s, LogLevel& out);

private:
    Logger() = default;

    LogLevel level_ = LogLevel::Info;   ///< 最低输出级别
};

}  // namespace dp

/// @brief 写 Debug 级别日志。
#define LOG_DEBUG(...) ::dp::Logger::instance().write(::dp::LogLevel::Debug, __VA_ARGS__)
/// @brief 写 Info 级别日志。
#define LOG_INFO(...)  ::dp::Logger::instance().write(::dp::LogLevel::Info,  __VA_ARGS__)
/// @brief 写 Warn 级别日志。
#define LOG_WARN(...)  ::dp::Logger::instance().write(::dp::LogLevel::Warn,  __VA_ARGS__)
/// @brief 写 Error 级别日志。
#define LOG_ERROR(...) ::dp::Logger::instance().write(::dp::LogLevel::Error, __VA_ARGS__)
