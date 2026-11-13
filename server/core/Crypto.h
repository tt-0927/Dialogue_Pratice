/*
 * 文件名:    Crypto.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  密码哈希用的最小加密工具（SHA-256 + 随机盐）。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#pragma once

#include <cstdint>
#include <string>

namespace dp {

/**
 * @brief 计算 SHA-256 摘要。
 * @param data 原始字节。
 * @retval 64 个小写十六进制字符。
 */
std::string sha256Hex(const std::string& data);

/**
 * @brief 生成密码学安全的随机盐。
 * @param bytes 盐的字节数，建议 16。
 * @retval 十六进制字符串（长度为 bytes * 2）。
 * @details 优先用 /dev/urandom；不可用时退化为时间+地址混合，并打印警告。
 */
std::string randomSalt(size_t bytes = 16);

/**
 * @brief 用盐计算密码哈希。
 * @param salt     盐（十六进制字符串）。
 * @param password 明文密码。
 * @retval 64 个小写十六进制字符。
 * @details 计算 `sha256(salt + ":" + password)`。
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

/* namespace dp */
}
