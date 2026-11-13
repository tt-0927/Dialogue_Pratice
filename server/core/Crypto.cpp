/*
 * 文件名:    Crypto.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  SHA-256 与密码哈希的实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#include "server/core/Crypto.h"

#include <array>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <vector>

namespace dp {
namespace {

/* SHA-256 的 64 个轮常量。 */
constexpr std::array<uint32_t, 64> kRoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

/* 32 位循环右移。 */
/*
 * 功能描述:  执行 rotr 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  value  参数。
 * param [in]  bits  参数。
 * return:    返回值，含义见功能描述。
 */
inline uint32_t rotr(uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

/* namespace */
}

/*
 * 功能描述:  执行 sha256Hex 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  data  参数。
 * return:    返回值，含义见功能描述。
 */
std::string sha256Hex(const std::string& data) {
    std::array<uint32_t, 8> hash = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    /* ---- 填充：0x80 + 若干 0 + 64 位大端比特长度 ---- */
    std::vector<uint8_t> message(data.begin(), data.end());
    const uint64_t bitLength = static_cast<uint64_t>(message.size()) * 8;
    message.push_back(0x80);
    while (message.size() % 64 != 56) message.push_back(0x00);
    for (int i = 7; i >= 0; --i) {
        message.push_back(static_cast<uint8_t>((bitLength >> (i * 8)) & 0xFF));
    }

    /* ---- 逐块压缩 ---- */
    for (size_t offset = 0; offset < message.size(); offset += 64) {
        uint32_t w[64] = {0};
        for (int i = 0; i < 16; ++i) {
            const size_t p = offset + static_cast<size_t>(i) * 4;
            w[i] = (static_cast<uint32_t>(message[p]) << 24)
                   | (static_cast<uint32_t>(message[p + 1]) << 16)
                   | (static_cast<uint32_t>(message[p + 2]) << 8)
                   | static_cast<uint32_t>(message[p + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = hash[0], b = hash[1], c = hash[2], d = hash[3];
        uint32_t e = hash[4], f = hash[5], g = hash[6], h = hash[7];

        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;

            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }

        hash[0] += a; hash[1] += b; hash[2] += c; hash[3] += d;
        hash[4] += e; hash[5] += f; hash[6] += g; hash[7] += h;
    }

    /* ---- 转十六进制 ---- */
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (uint32_t word : hash) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            out.push_back(kHex[(word >> shift) & 0xF]);
        }
    }
    return out;
}

/*
 * 功能描述:  执行 randomSalt 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  bytes  参数。
 * return:    返回值，含义见功能描述。
 */
std::string randomSalt(size_t bytes) {
    std::string raw;
    raw.resize(bytes);

    if (FILE* fp = std::fopen("/dev/urandom", "rb")) {
        const size_t got = std::fread(raw.data(), 1, bytes, fp);
        std::fclose(fp);
        if (got == bytes) {
            static const char* kHex = "0123456789abcdef";
            std::string out;
            out.reserve(bytes * 2);
            for (unsigned char c : raw) {
                out.push_back(kHex[c >> 4]);
                out.push_back(kHex[c & 0xF]);
            }
            return out;
        }
    }

    /* 兜底：没有 urandom 时用时间 + 栈地址混合。安全性差，仅保证系统能起来。 */
    std::fprintf(stderr, "[crypto] /dev/urandom 不可用，盐的随机性下降\n");
    const uint64_t seed = static_cast<uint64_t>(std::time(nullptr))
                          ^ reinterpret_cast<uintptr_t>(&raw);
    return sha256Hex(std::to_string(seed)).substr(0, bytes * 2);
}

/*
 * 功能描述:  执行 hashPassword 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  salt  参数。
 * param [in]  password  参数。
 * return:    返回值，含义见功能描述。
 */
std::string hashPassword(const std::string& salt, const std::string& password) {
    return sha256Hex(salt + ":" + password);
}

bool verifyPassword(const std::string& salt, const std::string& password,
                    const std::string& expected) {
    const std::string actual = hashPassword(salt, password);
    if (actual.size() != expected.size()) return false;

    /* 常量时间比较：长度相同的前提下不提前返回，避免时间侧信道 */
    unsigned char diff = 0;
    for (size_t i = 0; i < actual.size(); ++i) {
        diff |= static_cast<unsigned char>(actual[i] ^ expected[i]);
    }
    return diff == 0;
}

/* namespace dp */
}
