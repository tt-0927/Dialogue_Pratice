/**
 * @file  ServerConfig.h
 * @brief 服务端可调参数的集中定义。
 * @details 所有参数都有合理默认值，main() 解析命令行后覆盖。配置在启动后只读，
 *          因此可以安全地被多个业务服务共享同一份实例。
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
struct ServerConfig {
    // ---- 网络 ----
    std::string bindAddr = "0.0.0.0";      ///< 监听地址，`0.0.0.0` 表示所有网卡
    uint16_t    port     = 9000;           ///< 监听端口
    int         backlog  = 512;            ///< listen(2) 的 backlog
    size_t      maxConnections = 4096;     ///< 同时在线连接上限，超出的直接关闭

    // ---- 心跳 ----
    int heartbeatTimeoutSec = 90;          ///< 超过该时长未收到任何帧即判定掉线

    // ---- 历史消息（每房间独立计算）----
    size_t historyEntries   = 200;         ///< 保留条数上限
    size_t historyBytes     = 64 * 1024;   ///< 保留字节上限
    size_t historySendLimit = 100;         ///< 进房时最多下发条数
    size_t historySendBytes = 40 * 1024;   ///< 进房时下发字节上限（给帧头留余量）

    // ---- 房间 ----
    int roomTtlSec = 300;                  ///< 空房滞留秒数，0 表示立即销毁

    // ---- 数据保留 ----
    /// @brief 聊天记录保留天数，超过的移入归档库。0 表示永不清理。
    int messageRetentionDays = 90;
    /// @brief 归档库所在目录（主库记录到期后搬到这里）。
    std::string archiveDir = "data/archive";

    // ---- 认证 ----
    /// @brief 是否强制要求账号密码登录。
    /// @details false（默认）：带 password 就校验，不带则按昵称匿名登录（兼容旧客户端）；
    ///          true：没有 password 或校验失败一律拒绝。
    bool requireAuth = false;

    /// @brief 是否允许客户端自助注册。
    /// @details 局域网/教学场景默认开启；公网部署建议关掉，改由管理员建号。
    bool allowRegistration = true;

    // ---- 数据库 ----
    /// @brief 用户库路径（相对工作目录），不存在会自动创建。
    std::string usersDbPath = "data/users.db";
    /// @brief 聊天记录库路径（相对工作目录），不存在会自动创建。
    std::string messagesDbPath = "data/messages.db";

    // ---- 下发给客户端的 LLM 配置 ----
    /// @brief 服务端持有的 LLM 配置文件路径（相对工作目录）。
    /// @details 登录时原样下发给客户端，客户端不必各自维护 llm.json。
    ///          留空则不下发，客户端回退读本地配置。
    std::string llmConfigPath = "config/llm.json";
};

}  // namespace dp
