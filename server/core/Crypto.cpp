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
 * 功能描述:  获取rotr。
 * 作者:      JaTin
 * param [in]   uValue           待设置的数值，类型 uint32_t。
 * param [in]   nBits            输入数据，类型 int。
 * return:    返回 inline uint32_t 类型的处理结果，具体含义见功能描述。
 */
inline uint32_t rotr(uint32_t uValue, int nBits) {
    return (uValue >> nBits) | (uValue << (32 - nBits));
}

/* namespace */
}

/*
 * 功能描述:  获取sha256hex。
 * 作者:      JaTin
 * param [in]   strData          原始字节数据，类型 const std::string&。
 * return:    返回构造好的字符串；无内容时返回空字符串。
 */
std::string sha256Hex(const std::string& strData) {
    std::array<uint32_t, 8> uHash = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    /* ---- 填充：0x80 + 若干 0 + 64 位大端比特长度 ---- */
    std::vector<uint8_t> uMessage(strData.begin(), strData.end());
    const uint64_t uBitLength = static_cast<uint64_t>(uMessage.size()) * 8;
    uMessage.push_back(0x80);
    while (uMessage.size() % 64 != 56) uMessage.push_back(0x00);
    for (int i = 7; i >= 0; --i) {
        uMessage.push_back(static_cast<uint8_t>((uBitLength >> (i * 8)) & 0xFF));
    }

    /* ---- 逐块压缩 ---- */
    for (size_t offset = 0; offset < uMessage.size(); offset += 64) {
        uint32_t uW[64] = {0};
        for (int i = 0; i < 16; ++i) {
            const size_t uP = offset + static_cast<size_t>(i) * 4;
            uW[i] = (static_cast<uint32_t>(uMessage[uP]) << 24)
                   | (static_cast<uint32_t>(uMessage[uP + 1]) << 16)
                   | (static_cast<uint32_t>(uMessage[uP + 2]) << 8)
                   | static_cast<uint32_t>(uMessage[uP + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t uS0 = rotr(uW[i - 15], 7) ^ rotr(uW[i - 15], 18) ^ (uW[i - 15] >> 3);
            const uint32_t uS1 = rotr(uW[i - 2], 17) ^ rotr(uW[i - 2], 19) ^ (uW[i - 2] >> 10);
            uW[i] = uW[i - 16] + uS0 + uW[i - 7] + uS1;
        }

        uint32_t uA = uHash[0], b = uHash[1], c = uHash[2], d = uHash[3];
        uint32_t stE = uHash[4], f = uHash[5], g = uHash[6], stH = uHash[7];

        for (int i = 0; i < 64; ++i) {
            const uint32_t uS1 = rotr(stE, 6) ^ rotr(stE, 11) ^ rotr(stE, 25);
            const uint32_t uCh = (stE & f) ^ (~stE & g);
            const uint32_t uTemp1 = stH + uS1 + uCh + kRoundConstants[i] + uW[i];
            const uint32_t uS0 = rotr(uA, 2) ^ rotr(uA, 13) ^ rotr(uA, 22);
            const uint32_t uMaj = (uA & b) ^ (uA & c) ^ (b & c);
            const uint32_t uTemp2 = uS0 + uMaj;

            stH = g; g = f; f = stE; stE = d + uTemp1;
            d = c; c = b; b = uA; uA = uTemp1 + uTemp2;
        }

        uHash[0] += uA; uHash[1] += b; uHash[2] += c; uHash[3] += d;
        uHash[4] += stE; uHash[5] += f; uHash[6] += g; uHash[7] += stH;
    }

    /* ---- 转十六进制 ---- */
    static const char* strKHex = "0123456789abcdef";
    std::string strOut;
    strOut.reserve(64);
    for (uint32_t uWord : uHash) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            strOut.push_back(strKHex[(uWord >> shift) & 0xF]);
        }
    }
    return strOut;
}

/*
 * 功能描述:  获取randomsalt。
 * 作者:      JaTin
 * param [in]   uBytes           字节数，类型 size_t。
 * return:    返回构造好的字符串；无内容时返回空字符串。
 */
std::string randomSalt(size_t uBytes) {
    std::string strRaw;
    strRaw.resize(uBytes);

    if (FILE* fp = std::fopen("/dev/urandom", "rb")) {
        const size_t uGot = std::fread(strRaw.data(), 1, uBytes, fp);
        std::fclose(fp);
        if (uGot == uBytes) {
            static const char* strKHex = "0123456789abcdef";
            std::string strOut;
            strOut.reserve(uBytes * 2);
            for (unsigned char c : strRaw) {
                strOut.push_back(strKHex[c >> 4]);
                strOut.push_back(strKHex[c & 0xF]);
            }
            return strOut;
        }
    }

    /* 兜底：没有 urandom 时用时间 + 栈地址混合。安全性差，仅保证系统能起来。 */
    std::fprintf(stderr, "[crypto] /dev/urandom 不可用，盐的随机性下降\n");
    const uint64_t uSeed = static_cast<uint64_t>(std::time(nullptr))
                          ^ reinterpret_cast<uintptr_t>(&strRaw);
    return sha256Hex(std::to_string(uSeed)).substr(0, uBytes * 2);
}

/*
 * 功能描述:  计算摘要密码。
 * 作者:      JaTin
 * param [in]   strSalt          输入数据，类型 const std::string&。
 * param [in]   strPassword      登录密码，类型 const std::string&。
 * return:    返回构造好的字符串；无内容时返回空字符串。
 */
std::string hashPassword(const std::string& strSalt, const std::string& strPassword) {
    return sha256Hex(strSalt + ":" + strPassword);
}

/*
 * 功能描述:  校验密码；失败时返回 false。
 * 作者:      JaTin
 * param [in]   strSalt          输入数据，类型 const std::string&。
 * param [in]   strPassword      登录密码，类型 const std::string&。
 * param [in]   strExpected      输入数据，类型 const std::string&。
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool verifyPassword(const std::string& strSalt, const std::string& strPassword,
                    const std::string& strExpected) {
    const std::string strActual = hashPassword(strSalt, strPassword);
    if (strActual.size() != strExpected.size()) return false;

    /* 常量时间比较：长度相同的前提下不提前返回，避免时间侧信道 */
    unsigned char diff = 0;
    for (size_t i = 0; i < strActual.size(); ++i) {
        diff |= static_cast<unsigned char>(strActual[i] ^ strExpected[i]);
    }
    return diff == 0;
}

/* namespace dp */
}
