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

CServer::CServer(Config cfg)
    : m_stCfg(std::move(cfg)), m_stCtx(std::make_unique<CServerContext>(m_stCfg)) {}

/*
 * 功能描述:  执行 CServer 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
CServer::~CServer() {
    /* 先销毁事件循环（它持有 fd 与 epoll），再清空服务注册表，最后销毁上下文。 */
    reactor_.reset();
    CServiceRegistry::instance().clear();
    m_stCtx.reset();
}

/*
 * 功能描述:  执行 loadLlmConfig 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
bool CServer::loadLlmConfig() {
    const std::string& path = m_stCfg.llmConfigPath;
    if (path.empty()) {
        LOG_INFO("llmConfigPath 为空，不下发 LLM 配置（客户端使用本地配置）");
        return false;
    }

    std::ifstream file(path);
    if (!file) {
        LOG_WARN("未找到 LLM 配置 %s（客户端将回退读本地配置）", path.c_str());
        return false;
    }

    const std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    file.close();

    Json cfg = Json::parse(content, nullptr, false);
    if (cfg.is_discarded() || !cfg.is_object()) {
        LOG_ERROR("LLM 配置不是合法 JSON 对象：%s", path.c_str());
        return false;
    }

    m_stCtx->setLlmConfig(std::move(cfg));
    LOG_INFO("LLM 配置已加载（%s），登录时下发给客户端", path.c_str());
    return true;
}

/*
 * 功能描述:  执行 openDatabases 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
bool CServer::openDatabases() {
    std::string error;

    const auto ensureParent = [](const std::string& path) {
        const auto parent = std::filesystem::path(path).parent_path();
        if (parent.empty()) return;
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    };

    /* ---- 用户库 ---- */
    ensureParent(m_stCfg.usersDbPath);
    if (!m_stCtx->users().open(m_stCfg.usersDbPath, &error)) {
        LOG_ERROR("打开用户库失败（%s）：%s", m_stCfg.usersDbPath.c_str(), error.c_str());
        return false;
    }
    if (!m_stCtx->users().ensureSchema(&error)) {
        LOG_ERROR("初始化用户表失败：%s", error.c_str());
        return false;
    }
    LOG_INFO("用户库就绪：%s（%zu 个用户）",
             m_stCfg.usersDbPath.c_str(), m_stCtx->users().count());

    /* ---- 聊天记录库 ---- */
    ensureParent(m_stCfg.messagesDbPath);
    if (!m_stCtx->messages().open(m_stCfg.messagesDbPath, &error)) {
        LOG_ERROR("打开聊天记录库失败（%s）：%s",
                  m_stCfg.messagesDbPath.c_str(), error.c_str());
        return false;
    }
    if (!m_stCtx->messages().ensureSchema(&error)) {
        LOG_ERROR("初始化聊天记录表失败：%s", error.c_str());
        return false;
    }
    LOG_INFO("聊天记录库就绪：%s（%zu 条记录）",
             m_stCfg.messagesDbPath.c_str(), m_stCtx->messages().count());

    seedDefaultUsers();
    return true;
}

/*
 * 功能描述:  执行 seedDefaultUsers 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    void
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
    const Seed_S seeds[] = {
        {"alice", "alice", 24, "英语学习者，喜欢徒步"},
        {"bob",   "bob",   28, "英语母语者，来练中文"},
        {"carol", "carol", 22, "大学生，准备雅思"},
        {"dave",  "dave",  31, "程序员，想练口语"},
    };

    std::string error;
    for (const Seed_S& seed : seeds) {
        if (m_stCtx->users().createUser(seed.username, "123456", seed.nickname,
                                     seed.age, seed.description, &error)) {
            LOG_INFO("创建默认用户：%s（昵称 %s，初始密码 123456）",
                     seed.username, seed.nickname);
        } else {
            LOG_WARN("创建默认用户 %s 失败：%s", seed.username, error.c_str());
        }
    }
}

/*
 * 功能描述:  执行 onSecondTick 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    void
 */
void CServer::onSecondTick() {
    if (m_stCfg.messageRetentionDays <= 0) return;

    const auto now = std::chrono::steady_clock::now();
    if (now - m_stLastPrune < std::chrono::hours(24)) return;
    m_stLastPrune = now;
    pruneMessages();
}

/*
 * 功能描述:  执行 pruneMessages 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    void
 */
void CServer::pruneMessages() {
    std::error_code ec;
    std::filesystem::create_directories(m_stCfg.archiveDir, ec);

    const std::string archivePath = m_stCfg.archiveDir + "/messages_archive.db";
    std::string error;
    const int moved = m_stCtx->messages().archiveAndPrune(m_stCfg.messageRetentionDays,
                                                       archivePath, &error);

    if (!error.empty()) {
        LOG_WARN("归档清理失败：%s", error.c_str());
        return;
    }
    if (moved > 0) {
        LOG_INFO("归档完成：%d 条超过 %d 天的记录已移入 %s，主库剩余 %zu 条",
                 moved, m_stCfg.messageRetentionDays, archivePath.c_str(),
                 m_stCtx->messages().count());
    } else {
        LOG_DEBUG("归档检查：没有到期记录（保留 %d 天）", m_stCfg.messageRetentionDays);
    }
}

/*
 * 功能描述:  执行 start 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
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
    auto userService  = std::make_unique<CUserService>(*m_stCtx);
    auto roomService  = std::make_unique<CRoomService>(*m_stCtx);
    auto chatService  = std::make_unique<ChatService>(*m_stCtx);
    auto voiceService = std::make_unique<CVoiceService>(*m_stCtx);

    CRoomService* roomServiceRaw = roomService.get();
    reactor_->setSessionCloseHook([roomServiceRaw](CSession& session) {
        /* 连接断开等价于主动离房：通知房友并处理空房滞留。 */
        roomServiceRaw->leaveRoom(session, true);
    });

    CServiceRegistry& registry = CServiceRegistry::instance();
    registry.registerService(std::move(userService));
    registry.registerService(std::move(roomService));
    registry.registerService(std::move(chatService));
    registry.registerService(std::move(voiceService));

    LOG_INFO("services registered: %zu instance(s)", registry.size());

    if (!reactor_->start()) {
        LOG_ERROR("reactor start failed");
        return false;
    }

    m_nStarted = true;
    return true;
}

/*
 * 功能描述:  执行 run 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    void
 */
void CServer::run() {
    if (!m_nStarted) {
        LOG_ERROR("CServer::run() called before start()");
        return;
    }
    reactor_->run();
}

/*
 * 功能描述:  执行 stop 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    void
 */
void CServer::stop() {
    if (reactor_) reactor_->stop();
}

/* namespace dp */
}
