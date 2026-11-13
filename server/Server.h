/**
 * @file  Server.h
 * @brief 服务端门面：组装事件循环与各业务服务，管理它们的生命周期。
 * @details 本类不含任何业务逻辑，只做「接线」。分层关系：
 *
 *          ```
 *          Server（组合根，本类）
 *            ├── ServerContext   共享状态 + 出站能力（core）
 *            ├── Reactor         epoll 事件循环（core）
 *            └── ServiceRegistry opcode -> Service 映射（core）
 *                  ├── UserService   登录 / 改资料 / 心跳（business）
 *                  ├── RoomService   进房 / 离房 / 历史（business）
 *                  ├── ChatService   文本消息（business）
 *                  └── VoiceService  语音消息（media）
 *          ```
 */

#pragma once

#include <chrono>
#include <memory>

#include "server/core/ServerConfig.h"

namespace dp {

class Reactor;
class ServerContext;

/**
 * @brief 服务端实例。
 *
 * @details 生命周期顺序由本类保证：
 *          构造 → start()（建 socket、注册服务）→ run()（阻塞）→ 析构。
 *          所有成员以 `std::unique_ptr` 独占持有，不使用裸 new/delete。
 */
class Server {
public:
    /// @brief 服务端参数，等价于 ServerConfig；保留别名以兼容既有调用。
    using Config = ServerConfig;

    /**
     * @brief 构造服务端（不建立任何 socket）。
     * @param cfg 启动参数。
     */
    explicit Server(Config cfg);

    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /**
     * @brief 创建监听 socket 并注册业务服务。
     * @retval true  初始化完成，可以调用 run()。
     * @retval false 任一环节失败，原因已写入日志。
     */
    bool start();

    /**
     * @brief 进入事件循环，阻塞直到收到 SIGINT/SIGTERM 或 stop()。
     */
    void run();

    /// @brief 请求退出事件循环。
    void stop();

private:
    /**
     * @brief 读取并缓存待下发给客户端的 LLM 配置。
     * @retval true  配置已加载，登录时会随 LoginResp 下发。
     * @retval false 文件缺失或格式非法；客户端将回退读自己的本地配置。
     */
    bool loadLlmConfig();

    /**
     * @brief 打开用户库与聊天记录库，建表并补种子用户。
     * @retval true  两个库都可用。
     * @retval false 任一库打开或建表失败（服务端应拒绝启动）。
     * @details 数据库是持久层：打不开时继续运行会让「注册/历史」静默失效，
     *          不如直接启动失败，问题暴露得更早。
     */
    bool openDatabases();

    /// @brief users 表为空时插入几个默认账号，方便首次部署直接试用。
    void seedDefaultUsers();

    /// @brief 每秒被 Reactor 调一次，内部判断是否到了归档时间。
    void onSecondTick();

    /// @brief 把超过保留期的聊天记录移入归档库。
    void pruneMessages();

    Config                         cfg_;      ///< 启动参数副本
    std::unique_ptr<ServerContext> ctx_;      ///< 共享上下文
    std::unique_ptr<Reactor>       reactor_;  ///< 事件循环
    bool                           started_ = false;
    std::chrono::steady_clock::time_point lastPrune_;  ///< 上次归档时间
};

}  // namespace dp
