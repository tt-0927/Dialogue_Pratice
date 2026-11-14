/**
 * @file       Logger.cpp
 * @brief      CLogger 实现文件，包含相关接口的具体实现。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

/**
 * @brief  server/CLogger.cpp。
 */
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

/**
 * @brief  namespace。
 */
}

/**
 * @brief      获取本类的进程内单例对象。
 * @note       本函数无输入参数。
 * @return     返回 CLogger& CLogger:: 类型的对象引用或指针，调用方按接口约定使用。
 * @author     JaTin
 */
CLogger& CLogger::instance() {
    static CLogger stInst;
    return stInst;
}

/**
 * @brief      把日志级别字符串解析为日志级别枚举值。
 * @param[in] strS           目标会话对象；类型 const char*。
 * @param[out] enOut          输入数据；类型 LogLevel_EN&。
 * @return     成功返回 true；参数非法或执行失败时返回 false。
 * @author     JaTin
 */
bool CLogger::parseLevel(const char* strS, LogLevel_EN& enOut) {
    if (strS == nullptr) return false;
    if (std::strcmp(strS, "debug") == 0) { enOut = LogLevel_EN::Debug; return true; }
    if (std::strcmp(strS, "info")  == 0) { enOut = LogLevel_EN::Info;  return true; }
    if (std::strcmp(strS, "warn")  == 0) { enOut = LogLevel_EN::Warn;  return true; }
    if (std::strcmp(strS, "error") == 0) { enOut = LogLevel_EN::Error; return true; }
    return false;
}

/**
 * @brief      按日志级别格式化并输出一条日志。
 * @param[in] enLv           输入数据；类型 LogLevel_EN。
 * @param[in] strFmt         格式化字符串；类型 const char*。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CLogger::write(LogLevel_EN enLv, const char* strFmt, ...) {
    if (enLv < m_stLevel) return;

    char chMsg[2048];
    va_list stAp;
    va_start(stAp, strFmt);
    std::vsnprintf(chMsg, sizeof(chMsg), strFmt, stAp);
    va_end(stAp);

    const auto stNow = std::chrono::system_clock::now();
    const std::time_t stSecs = std::chrono::system_clock::to_time_t(stNow);
    const auto stMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            stNow.time_since_epoch()).count() % 1000;

    std::tm stTm{};
    localtime_r(&stSecs, &stTm);
    char chTs[32];
    std::strftime(chTs, sizeof(chTs), "%H:%M:%S", &stTm);

    const char* strTag = "INFO ";
    const char* strColor = "";
    switch (enLv) {
        case LogLevel_EN::Debug: strTag = "DEBUG"; strColor = "\033[36m"; break;
        case LogLevel_EN::Info:  strTag = "INFO "; strColor = "\033[32m"; break;
        case LogLevel_EN::Warn:  strTag = "WARN "; strColor = "\033[33m"; break;
        case LogLevel_EN::Error: strTag = "ERROR"; strColor = "\033[31m"; break;
    }

    std::lock_guard<std::mutex> stLock(g_mu);
    if (!g_colorInit) {
        g_color = ::isatty(STDOUT_FILENO) != 0;
        g_colorInit = true;
    }
    if (g_color) {
        std::fprintf(stdout, "%s.%03d %s%s\033[0m %s\n", chTs, static_cast<int>(stMillis), strColor, strTag, chMsg);
    } else {
        std::fprintf(stdout, "%s.%03d %s %s\n", chTs, static_cast<int>(stMillis), strTag, chMsg);
    }
    std::fflush(stdout);
}

/**
 * @brief  namespace dp。
 */
}
