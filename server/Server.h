/*
 * 文件名:    Server.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  服务端门面：组装事件循环与各业务服务，管理它们的生命周期。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#pragma once

#include <chrono>
#include <memory>

#include "server/core/ServerConfig.h"

namespace dp {

class CReactor;
class CServerContext;

/**
 * @brief 服务端实例。
 *
 * @details 生命周期顺序由本类保证：
 *          构造 → start()（建 socket、注册服务）→ run()（阻塞）→ 析构。
 *          所有成员以 `std::unique_ptr` 独占持有，不使用裸 new/delete。
 */
class CServer {
public:
    /* 服务端参数，等价于 ServerConfig_S；保留别名以兼容既有调用。 */
    using Config = ServerConfig_S;

    /**
     * @brief 构造服务端（不建立任何 socket）。
     * @param cfg 启动参数。
     */
    explicit CServer(Config cfg);

    ~CServer();

    CServer(const CServer&) = delete;
    CServer& operator=(const CServer&) = delete;

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

    /* 请求退出事件循环。 */
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

    /* users 表为空时插入几个默认账号，方便首次部署直接试用。 */
    void seedDefaultUsers();

    /* 每秒被 CReactor 调一次，内部判断是否到了归档时间。 */
    void onSecondTick();

    /* 把超过保留期的聊天记录移入归档库。 */
    void pruneMessages();

    /* < 启动参数副本 */
    Config                         m_stCfg;
    /* < 共享上下文 */
    std::unique_ptr<CServerContext> m_stCtx;
    /* < 事件循环 */
    std::unique_ptr<CReactor>       reactor_;
    bool                           m_nStarted = false;
    /* < 上次归档时间 */
    std::chrono::steady_clock::time_point m_stLastPrune;
};

/* namespace dp */
}
