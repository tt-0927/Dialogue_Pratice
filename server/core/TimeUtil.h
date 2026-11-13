/*
 * 文件名:    TimeUtil.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  跨模块复用的时间工具（header-only）。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#pragma once

#include <chrono>
#include <cstdint>

namespace dp {

/**
 * @brief 当前 Unix 时间戳（毫秒）。
 * @retval 自 1970-01-01 起经过的毫秒数，用于消息时间与历史淘汰判断。
 */
inline uint64_t nowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

/* namespace dp */
}
