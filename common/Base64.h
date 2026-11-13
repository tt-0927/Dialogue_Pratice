/*
 * 文件名:    Base64.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  Base64 头文件，声明相关类型与接口。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

/* common/Base64.h */

/* 标准 Base64 编解码（RFC 4648，带 '=' 填充）。 */
/* 语音消息把二进制 Opus 容器塞进 JSON 字符串，客户端与服务端共用这一份实现。 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace dp {

inline std::string base64Encode(const std::string& input) {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve((input.size() + 2) / 3 * 4);

    const auto* p = reinterpret_cast<const unsigned char*>(input.data());
    size_t i = 0;
    for (; i + 2 < input.size(); i += 3) {
        const uint32_t n = (static_cast<uint32_t>(p[i]) << 16)
                           | (static_cast<uint32_t>(p[i + 1]) << 8)
                           | static_cast<uint32_t>(p[i + 2]);
        out += kTable[(n >> 18) & 0x3F];
        out += kTable[(n >> 12) & 0x3F];
        out += kTable[(n >> 6) & 0x3F];
        out += kTable[n & 0x3F];
    }

    const size_t rest = input.size() - i;
    if (rest == 1) {
        const uint32_t n = static_cast<uint32_t>(p[i]) << 16;
        out += kTable[(n >> 18) & 0x3F];
        out += kTable[(n >> 12) & 0x3F];
        out += "==";
    } else if (rest == 2) {
        const uint32_t n = (static_cast<uint32_t>(p[i]) << 16)
                           | (static_cast<uint32_t>(p[i + 1]) << 8);
        out += kTable[(n >> 18) & 0x3F];
        out += kTable[(n >> 12) & 0x3F];
        out += kTable[(n >> 6) & 0x3F];
        out += '=';
    }
    return out;
}

/* 解码；遇到非法字符或长度不合法时返回空串（调用方据此报 bad_voice）。 */
inline std::string base64Decode(std::string_view input) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };

    /* 先滤掉空白与换行（有些客户端会折行） */
    std::string clean;
    clean.reserve(input.size());
    for (char c : input) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        clean.push_back(c);
    }
    if (clean.empty() || clean.size() % 4 != 0) return {};

    std::string out;
    out.reserve(clean.size() / 4 * 3);

    for (size_t i = 0; i < clean.size(); i += 4) {
        int v[4] = {0, 0, 0, 0};
        int padding = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = clean[i + k];
            if (c == '=') {
                /* '=' 只允许出现在最后两位 */
                if (i + 4 != clean.size() || k < 2) return {};
                ++padding;
                v[k] = 0;
                continue;
            }
            /* '=' 之后不能再有数据 */
            if (padding > 0) return {};
            v[k] = value(c);
            if (v[k] < 0) return {};
        }

        const uint32_t n = (static_cast<uint32_t>(v[0]) << 18)
                           | (static_cast<uint32_t>(v[1]) << 12)
                           | (static_cast<uint32_t>(v[2]) << 6)
                           | static_cast<uint32_t>(v[3]);
        out.push_back(static_cast<char>((n >> 16) & 0xFF));
        if (padding < 2) out.push_back(static_cast<char>((n >> 8) & 0xFF));
        if (padding < 1) out.push_back(static_cast<char>(n & 0xFF));
    }
    return out;
}

/* namespace dp */
}
