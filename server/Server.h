/**
 * @file       Server.h
 * @brief      服务端门面：组装事件循环与各业务服务，管理它们的生命周期。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#pragma once

#include <chrono>
#include <memory>

#include "server/core/ServerConfig.h"

namespace dp {

class CReactor;
class CServerContext;

/**
 * @brief   服务端实例。
 * @details 生命周期顺序由本类保证： 构造 → start()（建 socket、注册服务）→ run()（阻塞）→ 析构。 所有成员以 `std::unique_ptr` 独占持有，不使用裸 new/delete。
 * @author  JaTin
 */
class CServer {
public:
    /**
     * @brief  服务端参数，等价于 ServerConfig_S；保留别名以兼容既有调用。
     */
    using Config = ServerConfig_S;

    /**
     * @brief      构造服务端（不建立任何 socket）。
     * @param[in] cfg            启动参数；类型 Config。
     * @return     构造/析构函数无返回值。
     * @author     JaTin
     */
    explicit CServer(Config cfg);

    /**
     * @brief      构造本对象，完成成员初始化与依赖注入。
     * @note       本函数无输入参数。
     * @return     构造/析构函数无返回值。
     * @author     JaTin
     */
    ~CServer();

    CServer(const CServer&) = delete;
    CServer& operator=(const CServer&) = delete;

    /**
     * @brief      创建监听 socket 并注册业务服务。
     * @note       本函数无输入参数。
     * @return     true  初始化完成，可以调用 run()；false 任一环节失败，原因已写入日志。
     * @author     JaTin
     */
    bool start();

    /**
     * @brief      进入事件循环，阻塞直到收到 SIGINT/SIGTERM 或 stop()。
     * @note       本函数无输入参数。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void run();

    /**
     * @brief      请求退出事件循环。
     * @note       本函数无输入参数。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void stop();

private:
    /**
     * @brief      读取并缓存待下发给客户端的 LLM 配置。
     * @note       本函数无输入参数。
     * @return     true  配置已加载，登录时会随 LoginResp 下发；false 文件缺失或格式非法；客户端将回退读自己的本地配置。
     * @author     JaTin
     */
    bool loadLlmConfig();

    /**
     * @brief      打开用户库与聊天记录库，建表并补种子用户。
     * @details    数据库是持久层：打不开时继续运行会让「注册/历史」静默失效， 不如直接启动失败，问题暴露得更早。
     * @note       本函数无输入参数。
     * @return     true  两个库都可用；false 任一库打开或建表失败（服务端应拒绝启动）。
     * @author     JaTin
     */
    bool openDatabases();

    /**
     * @brief      users 表为空时插入几个默认账号，方便首次部署直接试用。
     * @note       本函数无输入参数。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void seedDefaultUsers();

    /**
     * @brief      每秒被 CReactor 调一次，内部判断是否到了归档时间。
     * @note       本函数无输入参数。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void onSecondTick();

    /**
     * @brief      把超过保留期的聊天记录移入归档库。
     * @note       本函数无输入参数。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void pruneMessages();

    /**
     * @brief  启动参数副本
     * @note   类型 Config；类/结构体成员变量。
     */
    Config                         m_stCfg;
    /**
     * @brief  共享上下文
     * @note   类型 std::unique_ptr<CServerContext>；类/结构体成员变量。
     */
    std::unique_ptr<CServerContext> m_stCtx;
    /**
     * @brief  事件循环
     * @note   类型 std::unique_ptr<CReactor>；类/结构体成员变量。
     */
    std::unique_ptr<CReactor>       reactor_;
    /**
     * @brief  Started。 初值 = false。
     * @note   类型 bool；类/结构体成员变量。
     */
    bool                           m_nStarted = false;
    /**
     * @brief  上次归档时间
     * @note   类型 std::chrono::steady_clock::time_point；类/结构体成员变量。
     */
    std::chrono::steady_clock::time_point m_stLastPrune;
};

/**
 * @brief  namespace dp。
 */
}
