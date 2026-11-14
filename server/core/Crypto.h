/**
 * @file       Crypto.h
 * @brief      密码哈希用的最小加密工具（SHA-256 + 随机盐）。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#pragma once

#include <cstdint>
#include <string>

namespace dp {

/**
 * @brief      计算 SHA-256 摘要。
 * @param[in] data           原始字节；类型 const std::string&。
 * @return     64 个小写十六进制字符。
 * @author     JaTin
 */
std::string sha256Hex(const std::string& data);

/**
 * @brief      生成密码学安全的随机盐。
 * @details    优先用 /dev/urandom；不可用时退化为时间+地址混合，并打印警告。
 * @param[in] bytes          盐的字节数，建议 16；类型 size_t。
 * @return     十六进制字符串（长度为 bytes * 2）。
 * @author     JaTin
 */
std::string randomSalt(size_t bytes = 16);

/**
 * @brief      用盐计算密码哈希。
 * @details    计算 `sha256(salt + ":" + password)`。
 * @param[in] salt           盐（十六进制字符串）；类型 const std::string&。
 * @param[in] password       明文密码；类型 const std::string&。
 * @return     64 个小写十六进制字符。
 * @author     JaTin
 */
std::string hashPassword(const std::string& salt, const std::string& password);

/**
 * @brief 校验密码。
 * @param salt     用户记录里的盐。
 * @param password 待校验的明文。
 * @param expected 用户记录里的哈希。
 * @retval true 匹配。
 * @details 用常量时间比较，避免通过响应时间侧信道猜测哈希。
 */
bool verifyPassword(const std::string& salt, const std::string& password,
                    const std::string& expected);

/**
 * @brief  namespace dp。
 */
}
