/*
 * 文件名:    ServerConfig.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  服务端可调参数的集中定义。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace dp {

/**
 * @brief 服务端运行参数。
 *
 * @details 按用途分为四组：
 *          - 网络：监听地址、端口、backlog、连接上限
 *          - 心跳：判定掉线的静默时长
 *          - 历史：每房间的保留量与进房时的下发量
 *          - 房间：空房滞留时间（用于断线重连补上下文）
 */
struct ServerConfig_S {
    /* ---- 网络 ---- */
    /* < 监听地址，`0.0.0.0` 表示所有网卡 */
    std::string bindAddr = "0.0.0.0";
    /* < 监听端口 */
    uint16_t    port     = 9000;
    /* < listen(2) 的 backlog */
    int         backlog  = 512;
    /* < 同时在线连接上限，超出的直接关闭 */
    size_t      maxConnections = 4096;

    /* ---- 心跳 ---- */
    /* < 超过该时长未收到任何帧即判定掉线 */
    int heartbeatTimeoutSec = 90;

    /* ---- 历史消息（每房间独立计算）---- */
    /* < 保留条数上限 */
    size_t historyEntries   = 200;
    /* < 保留字节上限 */
    size_t historyBytes     = 64 * 1024;
    /* < 进房时最多下发条数 */
    size_t historySendLimit = 100;
    /* < 进房时下发字节上限（给帧头留余量） */
    size_t historySendBytes = 40 * 1024;

    /* ---- 房间 ---- */
    /* < 空房滞留秒数，0 表示立即销毁 */
    int roomTtlSec = 300;

    /* ---- 数据保留 ---- */
    /* 聊天记录保留天数，超过的移入归档库。0 表示永不清理。 */
    int messageRetentionDays = 90;
    /* 归档库所在目录（主库记录到期后搬到这里）。 */
    std::string archiveDir = "data/archive";

    /* ---- 认证 ---- */
    /* 是否强制要求账号密码登录。 */
    /* false（默认）：带 password 就校验，不带则按昵称匿名登录（兼容旧客户端）； */
    /* true：没有 password 或校验失败一律拒绝。 */
    bool requireAuth = false;

    /* 是否允许客户端自助注册。 */
    /* 局域网/教学场景默认开启；公网部署建议关掉，改由管理员建号。 */
    bool allowRegistration = true;

    /* ---- 数据库 ---- */
    /* 用户库路径（相对工作目录），不存在会自动创建。 */
    std::string usersDbPath = "data/users.db";
    /* 聊天记录库路径（相对工作目录），不存在会自动创建。 */
    std::string messagesDbPath = "data/messages.db";

    /* ---- 下发给客户端的 LLM 配置 ---- */
    /* 服务端持有的 LLM 配置文件路径（相对工作目录）。 */
    /* 登录时原样下发给客户端，客户端不必各自维护 llm.json。 */
    /* 留空则不下发，客户端回退读本地配置。 */
    std::string llmConfigPath = "config/llm.json";
};

/* namespace dp */
}
