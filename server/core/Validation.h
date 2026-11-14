/**
 * @file       Validation.h
 * @brief      请求字段的合法性校验（header-only，供各业务服务复用）。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#pragma once

#include <cctype>
#include <string>

#include "common/Protocol.h"

namespace dp {

/**
 * @brief      去掉首尾空白。
 * @param[in] s              原字符串；类型 const std::string&。
 * @return     去除首尾空格、制表符、回车、换行后的副本；全空白时返回空串。
 * @author     JaTin
 */
inline std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return std::string();
    const size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

/**
 * @brief      校验房间号。
 * @param[in] id             房间号；类型 const std::string&。
 * @return     true  长度 1..32，且只含 `[A-Za-z0-9_-]`；false 为空、超长或含非法字符。
 * @author     JaTin
 */
inline bool validRoomId(const std::string& id) {
    if (id.empty() || id.size() > 32) return false;
    for (unsigned char c : id) {
        if (std::isalnum(c) == 0 && c != '_' && c != '-') return false;
    }
    return true;
}

/**
 * @brief      校验登录名。
 * @details    限制字符集是为了让登录名能安全地出现在日志、URL 与文件名里。
 * @param[in] name           登录名；类型 const std::string&。
 * @return     true  长度 3..24，且只含字母、数字、下划线、短横线；false 过短、过长或含其它字符。
 * @author     JaTin
 */
inline bool validUsername(const std::string& name) {
    if (name.size() < 3 || name.size() > 24) return false;
    for (unsigned char c : name) {
        if (std::isalnum(c) == 0 && c != '_' && c != '-') return false;
    }
    return true;
}

/**
 * @brief      校验密码。
 * @details    上限 64 是为了避免超长输入拖慢哈希计算。
 * @param[in] password       明文密码；类型 const std::string&。
 * @return     true  长度 6..64。
 * @author     JaTin
 */
inline bool validPassword(const std::string& password) {
    return password.size() >= 6 && password.size() <= 64;
}

/**
 * @brief      校验昵称。
 * @param[in] name           昵称；类型 const std::string&。
 * @return     true  长度 1..24 字节且不含控制字符；false 为空、超长或含控制字符。
 * @author     JaTin
 */
inline bool validName(const std::string& name) {
    if (name.empty() || name.size() > 24) return false;
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}

/**
 * @brief      校验聊天正文。
 * @param[in] text           正文；类型 const std::string&。
 * @return     true  非空且不超过 4000 字节；false 为空或超长。
 * @author     JaTin
 */
inline bool validText(const std::string& text) {
    return !text.empty() && text.size() <= 4000;
}

/**
 * @brief      校验个人简介。
 * @param[in] d              简介文本；类型 const std::string&。
 * @return     true  不超过 200 字节，允许换行，其余控制字符拒绝；false 超长或含非法控制字符。
 * @author     JaTin
 */
inline bool validDesc(const std::string& d) {
    if (d.size() > 200) return false;
    for (unsigned char c : d) {
        if (c < 0x20 && c != '\n') return false;
    }
    return true;
}

/**
 * @brief      从 JSON 中读取年龄。
 * @param[in] j              请求负载；类型 const Json&。
 * @param[out] out            读取成功时写入年龄（0 表示未填写，允许 1..150）；类型 int&。
 * @return     true  字段存在、是整数且落在 0..150；false 类型不符或越界，`out` 不被修改。
 * @author     JaTin
 */
inline bool readAge(const Json& j, int& out) {
    if (!j.contains("age") || !j["age"].is_number_integer()) return false;
    const int v = j["age"].get<int>();
    if (v < 0 || v > 150) return false;
    out = v;
    return true;
}

/**
 * @brief  namespace dp。
 */
}
