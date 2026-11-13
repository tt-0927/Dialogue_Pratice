// server/Logger.cpp
#include "server/core/Logger.h"

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

}  // namespace

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

bool Logger::parseLevel(const char* s, LogLevel& out) {
    if (s == nullptr) return false;
    if (std::strcmp(s, "debug") == 0) { out = LogLevel::Debug; return true; }
    if (std::strcmp(s, "info")  == 0) { out = LogLevel::Info;  return true; }
    if (std::strcmp(s, "warn")  == 0) { out = LogLevel::Warn;  return true; }
    if (std::strcmp(s, "error") == 0) { out = LogLevel::Error; return true; }
    return false;
}

void Logger::write(LogLevel lv, const char* fmt, ...) {
    if (lv < level_) return;

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
        case LogLevel::Debug: tag = "DEBUG"; color = "\033[36m"; break;
        case LogLevel::Info:  tag = "INFO "; color = "\033[32m"; break;
        case LogLevel::Warn:  tag = "WARN "; color = "\033[33m"; break;
        case LogLevel::Error: tag = "ERROR"; color = "\033[31m"; break;
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

}  // namespace dp
