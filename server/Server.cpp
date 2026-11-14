/*
 * 文件名:    Server.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  服务端组合根：创建上下文、事件循环与业务服务，并完成接线。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#include "server/Server.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <utility>

#include "server/business/ChatService.h"
#include "server/business/RoomService.h"
#include "server/business/UserService.h"
#include "server/core/Logger.h"
#include "server/core/Reactor.h"
#include "server/core/ServerContext.h"
#include "server/core/ServiceRegistry.h"
#include "server/media/VoiceService.h"

namespace dp {

/*
 * 功能描述:  构造 CServer 对象，完成成员初始化与依赖注入。
 * 作者:      JaTin
 * param [in]   stCfg            输入数据，类型 Config。
 * return:    无返回值（构造函数或析构函数）。
 */
CServer::CServer(Config stCfg)
    : m_stCfg(std::move(stCfg)), m_stCtx(std::make_unique<CServerContext>(m_stCfg)) {}

/*
 * 功能描述:  析构 CServer 对象，释放运行期持有的全部资源。
 * 作者:      JaTin
 * return:    无返回值（构造函数或析构函数）。
 */
CServer::~CServer() {
    /* 先销毁事件循环（它持有 fd 与 epoll），再清空服务注册表，最后销毁上下文。 */
    reactor_.reset();
    CServiceRegistry::instance().clear();
    m_stCtx.reset();
}

/*
 * 功能描述:  加载大模型配置；实现上异常路径记录错误或警告日志；失败时返回 false。
 * 作者:      JaTin
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CServer::loadLlmConfig() {
    const std::string& strPath = m_stCfg.llmConfigPath;
    if (strPath.empty()) {
        LOG_INFO("llmConfigPath 为空，不下发 LLM 配置（客户端使用本地配置）");
        return false;
    }

    std::ifstream stFile(strPath);
    if (!stFile) {
        LOG_WARN("未找到 LLM 配置 %s（客户端将回退读本地配置）", strPath.c_str());
        return false;
    }

    const std::string content((std::istreambuf_iterator<char>(stFile)),
                              std::istreambuf_iterator<char>());
    stFile.close();

    Json stCfg = Json::parse(content, nullptr, false);
    if (stCfg.is_discarded() || !stCfg.is_object()) {
        LOG_ERROR("LLM 配置不是合法 JSON 对象：%s", strPath.c_str());
        return false;
    }

    m_stCtx->setLlmConfig(std::move(stCfg));
    LOG_INFO("LLM 配置已加载（%s），登录时下发给客户端", strPath.c_str());
    return true;
}

/*
 * 功能描述:  获取opendatabases；实现上异常路径记录错误或警告日志；失败时返回 false。
 * 作者:      JaTin
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CServer::openDatabases() {
    std::string strError;

    const auto stEnsureParent = [](const std::string& path) {
        const auto stParent = std::filesystem::path(path).parent_path();
        if (stParent.empty()) return;
        std::error_code stEc;
        std::filesystem::create_directories(stParent, stEc);
    };

    /* ---- 用户库 ---- */
    stEnsureParent(m_stCfg.usersDbPath);
    if (!m_stCtx->users().open(m_stCfg.usersDbPath, &strError)) {
        LOG_ERROR("打开用户库失败（%s）：%s", m_stCfg.usersDbPath.c_str(), strError.c_str());
        return false;
    }
    if (!m_stCtx->users().ensureSchema(&strError)) {
        LOG_ERROR("初始化用户表失败：%s", strError.c_str());
        return false;
    }
    LOG_INFO("用户库就绪：%s（%zu 个用户）",
             m_stCfg.usersDbPath.c_str(), m_stCtx->users().count());

    /* ---- 聊天记录库 ---- */
    stEnsureParent(m_stCfg.messagesDbPath);
    if (!m_stCtx->messages().open(m_stCfg.messagesDbPath, &strError)) {
        LOG_ERROR("打开聊天记录库失败（%s）：%s",
                  m_stCfg.messagesDbPath.c_str(), strError.c_str());
        return false;
    }
    if (!m_stCtx->messages().ensureSchema(&strError)) {
        LOG_ERROR("初始化聊天记录表失败：%s", strError.c_str());
        return false;
    }
    LOG_INFO("聊天记录库就绪：%s（%zu 条记录）",
             m_stCfg.messagesDbPath.c_str(), m_stCtx->messages().count());

    seedDefaultUsers();
    return true;
}

/*
 * 功能描述:  执行seeddefault 用户表；实现上异常路径记录错误或警告日志。
 * 作者:      JaTin
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CServer::seedDefaultUsers() {
    if (m_stCtx->users().count() > 0) return;

    struct Seed_S {
        const char* username;
        const char* nickname;
        int         age;
        const char* description;
    };

    /* 初始密码统一 123456：仅用于首次部署验证，正式使用应改掉。 */
    /* 昵称与登录名保持一致，避免「填 alice 匹配不到 Alice」这类歧义。 */
    /* 需要好看的显示名时，直接改库里的 nickname 即可。 */
    const Seed_S stSeeds[] = {
        {"alice", "alice", 24, "英语学习者，喜欢徒步"},
        {"bob",   "bob",   28, "英语母语者，来练中文"},
        {"carol", "carol", 22, "大学生，准备雅思"},
        {"dave",  "dave",  31, "程序员，想练口语"},
    };

    std::string strError;
    for (const Seed_S& stSeed : stSeeds) {
        if (m_stCtx->users().createUser(stSeed.username, "123456", stSeed.nickname,
                                     stSeed.age, stSeed.description, &strError)) {
            LOG_INFO("创建默认用户：%s（昵称 %s，初始密码 123456）",
                     stSeed.username, stSeed.nickname);
        } else {
            LOG_WARN("创建默认用户 %s 失败：%s", stSeed.username, strError.c_str());
        }
    }
}

/*
 * 功能描述:  每秒定时器回调：回收空闲会话、归档过期消息并输出统计信息。
 * 作者:      JaTin
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CServer::onSecondTick() {
    if (m_stCfg.messageRetentionDays <= 0) return;

    const auto stNow = std::chrono::steady_clock::now();
    if (stNow - m_stLastPrune < std::chrono::hours(24)) return;
    m_stLastPrune = stNow;
    pruneMessages();
}

/*
 * 功能描述:  执行prune 消息记录；实现上异常路径记录错误或警告日志。
 * 作者:      JaTin
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CServer::pruneMessages() {
    std::error_code stEc;
    std::filesystem::create_directories(m_stCfg.archiveDir, stEc);

    const std::string strArchivePath = m_stCfg.archiveDir + "/messages_archive.db";
    std::string strError;
    const int nMoved = m_stCtx->messages().archiveAndPrune(m_stCfg.messageRetentionDays,
                                                       strArchivePath, &strError);

    if (!strError.empty()) {
        LOG_WARN("归档清理失败：%s", strError.c_str());
        return;
    }
    if (nMoved > 0) {
        LOG_INFO("归档完成：%d 条超过 %d 天的记录已移入 %s，主库剩余 %zu 条",
                 nMoved, m_stCfg.messageRetentionDays, strArchivePath.c_str(),
                 m_stCtx->messages().count());
    } else {
        LOG_DEBUG("归档检查：没有到期记录（保留 %d 天）", m_stCfg.messageRetentionDays);
    }
}

/*
 * 功能描述:  启动start；实现上异常路径记录错误或警告日志；失败时返回 false。
 * 作者:      JaTin
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CServer::start() {
    if (m_nStarted) return true;

    if (!openDatabases()) {
        LOG_ERROR("数据库初始化失败，服务端拒绝启动");
        return false;
    }

    loadLlmConfig();

    reactor_ = std::make_unique<CReactor>(*m_stCtx);
    m_stCtx->attachReactor(reactor_.get());

    /* 启动时先归档一次（把停机期间过期的记录清掉），之后每 24 小时一次 */
    m_stLastPrune = std::chrono::steady_clock::now();
    if (m_stCfg.messageRetentionDays > 0) pruneMessages();
    reactor_->setSecondHook([this] { onSecondTick(); });

    /* ---- 注册业务服务 ------------------------------------------------------ */
    /* 各服务以 unique_ptr 交给注册表独占持有；这里额外保留 CRoomService 的裸指针， */
    /* 因为「会话销毁时退出房间」的清理动作需要回调它（生命周期由注册表保证）。 */
    auto stUserService  = std::make_unique<CUserService>(*m_stCtx);
    auto stRoomService  = std::make_unique<CRoomService>(*m_stCtx);
    auto stChatService  = std::make_unique<ChatService>(*m_stCtx);
    auto stVoiceService = std::make_unique<CVoiceService>(*m_stCtx);

    CRoomService* pRoomServiceRaw = stRoomService.get();
    reactor_->setSessionCloseHook([pRoomServiceRaw](CSession& session) {
        /* 连接断开等价于主动离房：通知房友并处理空房滞留。 */
        pRoomServiceRaw->leaveRoom(session, true);
    });

    CServiceRegistry& stRegistry = CServiceRegistry::instance();
    stRegistry.registerService(std::move(stUserService));
    stRegistry.registerService(std::move(stRoomService));
    stRegistry.registerService(std::move(stChatService));
    stRegistry.registerService(std::move(stVoiceService));

    LOG_INFO("services registered: %zu instance(s)", stRegistry.size());

    if (!reactor_->start()) {
        LOG_ERROR("reactor start failed");
        return false;
    }

    m_nStarted = true;
    return true;
}

/*
 * 功能描述:  运行run；实现上异常路径记录错误或警告日志。
 * 作者:      JaTin
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CServer::run() {
    if (!m_nStarted) {
        LOG_ERROR("CServer::run() called before start()");
        return;
    }
    reactor_->run();
}

/*
 * 功能描述:  停止stop。
 * 作者:      JaTin
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CServer::stop() {
    if (reactor_) reactor_->stop();
}

/* namespace dp */
}
