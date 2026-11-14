/**
 * @file       TimeUtil.h
 * @brief      跨模块复用的时间工具（header-only）。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#pragma once

#include <chrono>
#include <cstdint>

namespace dp {

/**
 * @brief      当前 Unix 时间戳（毫秒）。
 * @note       本函数无输入参数。
 * @return     自 1970-01-01 起经过的毫秒数，用于消息时间与历史淘汰判断。
 * @author     JaTin
 */
inline uint64_t nowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

/**
 * @brief  namespace dp。
 */
}
