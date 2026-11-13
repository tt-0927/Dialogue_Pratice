/*
 * 文件名:    Logger.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  CLogger 实现文件，包含相关接口的具体实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

/* server/CLogger.cpp */
#include "server/Logger.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

#include <unistd.h>

namespace dp {
namespace {

std::mutex g_mu;
bool g_color = false;
bool g_colorInit = false;

/* namespace */
}

/*
 * 功能描述:  执行 instance 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
CLogger& CLogger::instance() {
    static CLogger inst;
    return inst;
}

/*
 * 功能描述:  执行 parseLevel 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  s  参数。
 * param [out]  out  参数。
 * return:    返回值，含义见功能描述。
 */
bool CLogger::parseLevel(const char* s, LogLevel_EN& out) {
    if (s == nullptr) return false;
    if (std::strcmp(s, "debug") == 0) { out = LogLevel_EN::Debug; return true; }
    if (std::strcmp(s, "info")  == 0) { out = LogLevel_EN::Info;  return true; }
    if (std::strcmp(s, "warn")  == 0) { out = LogLevel_EN::Warn;  return true; }
    if (std::strcmp(s, "error") == 0) { out = LogLevel_EN::Error; return true; }
    return false;
}

/*
 * 功能描述:  执行 write 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  lv  参数。
 * param [in]  fmt  参数。
 * param [in]  param  参数。
 * return:    void
 */
void CLogger::write(LogLevel_EN lv, const char* fmt, ...) {
    if (lv < m_stLevel) return;

    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    const auto now = std::chrono::system_clock::now();
    const std::time_t secs = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()).count() % 1000;

    std::tm tm{};
    localtime_r(&secs, &tm);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%H:%M:%S", &tm);

    const char* tag = "INFO ";
    const char* color = "";
    switch (lv) {
        case LogLevel_EN::Debug: tag = "DEBUG"; color = "\033[36m"; break;
        case LogLevel_EN::Info:  tag = "INFO "; color = "\033[32m"; break;
        case LogLevel_EN::Warn:  tag = "WARN "; color = "\033[33m"; break;
        case LogLevel_EN::Error: tag = "ERROR"; color = "\033[31m"; break;
    }

    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_colorInit) {
        g_color = ::isatty(STDOUT_FILENO) != 0;
        g_colorInit = true;
    }
    if (g_color) {
        std::fprintf(stdout, "%s.%03d %s%s\033[0m %s\n", ts, static_cast<int>(millis), color, tag, msg);
    } else {
        std::fprintf(stdout, "%s.%03d %s %s\n", ts, static_cast<int>(millis), tag, msg);
    }
    std::fflush(stdout);
}

/* namespace dp */
}
