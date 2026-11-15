/**
 * @file       UserService.cpp
 * @brief      用户领域业务的实现。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释；
 *             2026-11-15 JaTin 增加头像的登录恢复、校验与持久化。
 */

#include "server/business/UserService.h"

#include <optional>

#include "common/Base64.h"
#include "server/core/Logger.h"
#include "server/core/TimeUtil.h"
#include "server/core/Validation.h"

namespace dp {
namespace {

/**
 * @brief      构造下发给客户端的资料快照。
 * @param[in] stS            会话；类型 const CSession&。
 * @return     含 userId / name / age / desc / avatar 的 JSON 对象。
 * @author     JaTin
 */
Json profileJson(const CSession& stS) {
    Json stP = Json::object();
    stP["userId"] = stS.id();
    stP["name"] = stS.name();
    stP["age"] = stS.age();
    stP["desc"] = stS.desc();
    stP["avatar"] = base64Encode(stS.avatar());
    return stP;
}

/**
 * @brief  namespace。
 */
}

/**
 * @brief      返回本业务服务所处理的协议操作码列表。
 * @note       本函数无输入参数。
 * @return     返回 vector<OpCode_EN> CUserService:: 类型的处理结果。
 * @author     JaTin
 */
std::vector<OpCode_EN> CUserService::opcodes() const {
    return {OpCode_EN::Login, OpCode_EN::Register, OpCode_EN::UpdateProfile, OpCode_EN::Ping};
}

/**
 * @brief      按操作码分发并处理一条业务请求。
 * @param[out] stSession      目标会话对象；类型 CSession&。
 * @param[in] enOp           协议操作码；类型 OpCode_EN。
 * @param[in] stPayload      协议载荷内容；类型 const Json&。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CUserService::handle(CSession& stSession, OpCode_EN enOp, const Json& stPayload) {
    switch (enOp) {
        case OpCode_EN::Login:         onLogin(stSession, stPayload);         break;
        case OpCode_EN::Register:      onRegister(stSession, stPayload);      break;
        case OpCode_EN::UpdateProfile: onUpdateProfile(stSession, stPayload); break;
        case OpCode_EN::Ping:          onPing(stSession, stPayload);          break;
        default:                                                       break;
    }
}

/**
 * @brief      完成登录 on；实现上异常路径记录错误或警告日志、向目标会话或整个房间下发数据。
 * @param[out] stS            目标会话对象；类型 CSession&。
 * @param[in] stJ            输入数据；类型 const Json&。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CUserService::onLogin(CSession& stS, const Json& stJ) {
    if (stS.loggedIn()) {
        m_stCtx.sendError(stS, "already_logged_in", "session already logged in");
        return;
    }
    if (!stJ.contains("name") || !stJ["name"].is_string()) {
        m_stCtx.sendError(stS, "bad_payload", "field 'name' (string) is required");
        return;
    }
    const std::string strName = trim(stJ["name"].get<std::string>());
    if (!validName(strName)) {
        m_stCtx.sendError(stS, "bad_name", "name must be 1..24 bytes without control chars");
        return;
    }

    int nAge = 0;
    if (stJ.contains("age") && !stJ["age"].is_null()) {
        if (!readAge(stJ, nAge)) {
            m_stCtx.sendError(stS, "bad_age", "age must be an integer in 0..150");
            return;
        }
    }

    std::string strDesc;
    if (stJ.contains("desc") && !stJ["desc"].is_null()) {
        if (!stJ["desc"].is_string()) {
            m_stCtx.sendError(stS, "bad_payload", "field 'desc' must be a string");
            return;
        }
        strDesc = trim(stJ["desc"].get<std::string>());
        if (!validDesc(strDesc)) {
            m_stCtx.sendError(stS, "bad_desc", "desc must be <= 200 bytes");
            return;
        }
    }

    /**
     * @brief  客户端带了 password 就走账号校验；没带则走兼容路径（昵称匹配或匿名）。
     */
    const bool nHasPassword = stJ.contains("password") && stJ["password"].is_string()
                             && !stJ["password"].get<std::string>().empty();

    int64_t lAccountId = 0;
    std::optional<UserRecord_S> stUser;

    if (nHasPassword) {
        UserRecord_S stVerified;
        if (!m_stCtx.users().verifyCredentials(strName, stJ["password"].get<std::string>(),
                                            &stVerified)) {
            /**
             * @brief  统一提示，不区分「账号不存在」和「密码错误」，避免账号枚举。
             */
            LOG_WARN("session#%llu 登录失败（账号 '%s'）",
                     static_cast<unsigned long long>(stS.id()), strName.c_str());
            m_stCtx.sendError(stS, "auth_failed", "账号或密码错误");
            return;
        }
        stUser = stVerified;
    } else if (m_stCtx.config().requireAuth) {
        m_stCtx.sendError(stS, "auth_required", "本服务器要求账号密码登录");
        return;
    } else {
        /**
         * @brief  兼容模式：先按登录名精确匹配，再按昵称忽略大小写匹配。
         */
        stUser = m_stCtx.users().findByUsername(strName);
        if (!stUser) stUser = m_stCtx.users().findByNickname(strName);
    }

    if (stUser) {
        lAccountId = stUser->id;
        stS.setName(stUser->nickname);
        stS.setAge(stUser->age);
        stS.setDesc(stUser->description);
        stS.setAvatar(stUser->m_strAvatar);
        m_stCtx.users().touchLogin(stUser->id);
        LOG_INFO("session#%llu 匹配到账号 #%lld（%s，%s）",
                 static_cast<unsigned long long>(stS.id()),
                 static_cast<long long>(stUser->id), stUser->username.c_str(),
                 nHasPassword ? "密码校验" : "昵称匹配");
    } else {
        stS.setName(strName);
        stS.setAge(nAge);
        stS.setDesc(strDesc);
        stS.setAvatar(std::string());
        LOG_INFO("session#%llu 匿名登录（昵称 '%s' 不在用户库）",
                 static_cast<unsigned long long>(stS.id()), strName.c_str());
    }
    stS.setAccountId(lAccountId);
    stS.setState(SessionState_EN::LoggedIn);

    Json stResp = profileJson(stS);
    stResp["ok"] = true;
    stResp["serverTime"] = nowMs();
    /**
     * @brief  0 表示匿名；客户端可用于区分身份来源。
     */
    stResp["accountId"] = lAccountId;

    /**
     * @brief  服务端统一配置：客户端不必各自维护 llm.json。 结构原样透传（由客户端的 AppConfig 解析），服务端不关心字段含义。
     */
    if (m_stCtx.hasLlmConfig()) {
        stResp["llmConfig"] = m_stCtx.llmConfig();
    }

    m_stCtx.sendTo(stS, OpCode_EN::LoginResp, stResp);

    Json stWelcome = Json::object();
    stWelcome["event"] = "welcome";
    stWelcome["text"] = "欢迎 " + strName + "，请用房间号加入房间";
    stWelcome["ts"] = nowMs();
    m_stCtx.sendTo(stS, OpCode_EN::SystemMsg, stWelcome);

    LOG_INFO("session#%llu login as '%s'",
             static_cast<unsigned long long>(stS.id()), strName.c_str());
}

/**
 * @brief      注册 on；实现上异常路径记录错误或警告日志、向目标会话或整个房间下发数据。
 * @param[out] stS            目标会话对象；类型 CSession&。
 * @param[in] stJ            输入数据；类型 const Json&。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CUserService::onRegister(CSession& stS, const Json& stJ) {
    /**
     * @brief  单连接注册次数上限：防止有人写个脚本批量刷号
     * @note   取值 3。
     */
    constexpr int nKMaxRegisterPerSession = 3;

    if (!m_stCtx.config().allowRegistration) {
        m_stCtx.sendError(stS, "register_disabled", "本服务器不开放自助注册");
        return;
    }
    if (stS.loggedIn()) {
        m_stCtx.sendError(stS, "already_logged_in", "已登录，无需注册");
        return;
    }
    if (stS.registerAttempts() >= nKMaxRegisterPerSession) {
        m_stCtx.sendError(stS, "too_many_registrations", "本连接注册次数过多，请重新连接");
        return;
    }
    stS.bumpRegisterAttempts();

    /**
     * @brief  ---- 必填：登录名 ----。
     */
    if (!stJ.contains("username") || !stJ["username"].is_string()) {
        m_stCtx.sendError(stS, "bad_payload", "field 'username' (string) is required");
        return;
    }
    const std::string strUsername = trim(stJ["username"].get<std::string>());
    if (!validUsername(strUsername)) {
        m_stCtx.sendError(stS, "bad_username",
                       "登录名需为 3..24 位的字母、数字、下划线或短横线");
        return;
    }

    /**
     * @brief  ---- 必填：密码 ----。
     */
    if (!stJ.contains("password") || !stJ["password"].is_string()) {
        m_stCtx.sendError(stS, "bad_payload", "field 'password' (string) is required");
        return;
    }
    const std::string strPassword = stJ["password"].get<std::string>();
    if (!validPassword(strPassword)) {
        m_stCtx.sendError(stS, "weak_password", "密码长度需为 6..64 位");
        return;
    }

    /**
     * @brief  ---- 可选：昵称（缺省用登录名）----。
     */
    std::string strNickname = strUsername;
    if (stJ.contains("nickname") && stJ["nickname"].is_string()) {
        strNickname = trim(stJ["nickname"].get<std::string>());
        if (!validName(strNickname)) {
            m_stCtx.sendError(stS, "bad_name", "昵称需为 1..24 字节且不含控制字符");
            return;
        }
    }

    /**
     * @brief  ---- 可选：年龄 ----。
     */
    int nAge = 0;
    if (stJ.contains("age") && !stJ["age"].is_null() && !readAge(stJ, nAge)) {
        m_stCtx.sendError(stS, "bad_age", "age must be an integer in 0..150");
        return;
    }

    /**
     * @brief  ---- 可选：简介 ----。
     */
    std::string strDesc;
    if (stJ.contains("desc") && stJ["desc"].is_string()) {
        strDesc = trim(stJ["desc"].get<std::string>());
        if (!validDesc(strDesc)) {
            m_stCtx.sendError(stS, "bad_desc", "desc must be <= 200 bytes");
            return;
        }
    }

    /**
     * @brief  ---- 唯一性检查 ----。
     */
    if (m_stCtx.users().findByUsername(strUsername)) {
        m_stCtx.sendError(stS, "username_taken", "该登录名已被占用");
        return;
    }

    std::string strError;
    if (!m_stCtx.users().createUser(strUsername, strPassword, strNickname, nAge, strDesc, &strError)) {
        LOG_ERROR("注册失败（%s）：%s", strUsername.c_str(), strError.c_str());
        m_stCtx.sendError(stS, "register_failed", "注册失败，请稍后再试");
        return;
    }

    Json stResp = Json::object();
    stResp["ok"] = true;
    stResp["username"] = strUsername;
    stResp["nickname"] = strNickname;
    if (auto created = m_stCtx.users().findByUsername(strUsername)) {
        stResp["userId"] = created->id;
    }
    m_stCtx.sendTo(stS, OpCode_EN::RegisterResp, stResp);

    LOG_INFO("session#%llu 注册新账号 '%s'（昵称 '%s'，年龄 %d）",
             static_cast<unsigned long long>(stS.id()), strUsername.c_str(),
             strNickname.c_str(), nAge);
}

/**
 * @brief      更新资料：只覆盖请求里出现的字段；持久账号同时写回数据库。
 * @details    头像字段：字符串为 Base64 JPEG（上限 kMaxAvatarBytes 字节）， null 表示清除头像恢复首字默认图。改名后会向同房间广播 rename 系统消息。
 * @param[out] stS            会话；类型 CSession&。
 * @param[in] stJ            输入数据；类型 const Json&。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CUserService::onUpdateProfile(CSession& stS, const Json& stJ) {
    if (!stS.loggedIn()) {
        m_stCtx.sendError(stS, "not_logged_in", "login first");
        return;
    }

    /**
     * @brief  只更新请求里出现的字段，没带的保持原值。
     */
    std::string strNewName = stS.name();
    if (stJ.contains("name") && !stJ["name"].is_null()) {
        if (!stJ["name"].is_string()) {
            m_stCtx.sendError(stS, "bad_payload", "field 'name' must be a string");
            return;
        }
        strNewName = trim(stJ["name"].get<std::string>());
        if (!validName(strNewName)) {
            m_stCtx.sendError(stS, "bad_name", "name must be 1..24 bytes without control chars");
            return;
        }
    }

    int nNewAge = stS.age();
    if (stJ.contains("age") && !stJ["age"].is_null()) {
        if (!readAge(stJ, nNewAge)) {
            m_stCtx.sendError(stS, "bad_age", "age must be an integer in 0..150");
            return;
        }
    }

    std::string strNewDesc = stS.desc();
    if (stJ.contains("desc") && !stJ["desc"].is_null()) {
        if (!stJ["desc"].is_string()) {
            m_stCtx.sendError(stS, "bad_payload", "field 'desc' must be a string");
            return;
        }
        strNewDesc = trim(stJ["desc"].get<std::string>());
        if (!validDesc(strNewDesc)) {
            m_stCtx.sendError(stS, "bad_desc", "desc must be <= 200 bytes");
            return;
        }
    }

    /* 头像：null 清除；字符串为 Base64 JPEG，解码后校验大小。 */
    std::string strNewAvatar = stS.avatar();
    if (stJ.contains("avatar")) {
        if (stJ["avatar"].is_null()) {
            strNewAvatar.clear();
        } else if (stJ["avatar"].is_string()) {
            std::string strDecoded = base64Decode(stJ["avatar"].get<std::string>());
            if (strDecoded.empty()) {
                m_stCtx.sendError(stS, "bad_avatar", "avatar must be valid base64");
                return;
            }
            if (strDecoded.size() > kMaxAvatarBytes) {
                m_stCtx.sendError(stS, "bad_avatar", "avatar must be <= 32 KiB after decode");
                return;
            }
            strNewAvatar = std::move(strDecoded);
        } else {
            m_stCtx.sendError(stS, "bad_payload", "field 'avatar' must be a string or null");
            return;
        }
    }

    const std::string strOldName = stS.name();
    const bool nRenamed = (strNewName != strOldName);

    stS.setName(strNewName);
    stS.setAge(nNewAge);
    stS.setDesc(strNewDesc);
    stS.setAvatar(strNewAvatar);

    /* 持久账号写回数据库；失败不影响本次会话生效，但记录日志供排查。 */
    if (stS.accountId() > 0) {
        UserRecord_S stRecord;
        stRecord.id = stS.accountId();
        stRecord.nickname = strNewName;
        stRecord.age = nNewAge;
        stRecord.description = strNewDesc;
        stRecord.m_strAvatar = strNewAvatar;
        std::string strError;
        if (!m_stCtx.users().updateProfile(stRecord, &strError)) {
            LOG_WARN("session#%llu 资料写库失败（account #%lld）：%s",
                     static_cast<unsigned long long>(stS.id()),
                     static_cast<long long>(stS.accountId()), strError.c_str());
        }
    }

    Json stResp = profileJson(stS);
    stResp["ok"] = true;
    m_stCtx.sendTo(stS, OpCode_EN::ProfileResp, stResp);

    /**
     * @brief  改名必须通知同房间，否则别人成员列表里还是旧名字。
     */
    if (nRenamed && !stS.room().empty()) {
        Json stSys = Json::object();
        stSys["event"] = "rename";
        stSys["room"] = stS.room();
        /**
         * @brief  客户端用它更新成员列表。
         */
        stSys["name"] = strNewName;
        stSys["text"] = strOldName + " 改名为 " + strNewName;
        stSys["ts"] = nowMs();
        m_stCtx.broadcastRoom(stS.room(), OpCode_EN::SystemMsg, stSys, 0);
    }

    LOG_INFO("session#%llu profile updated (name='%s', age=%d, desc=%zu B, avatar=%zu B%s)",
             static_cast<unsigned long long>(stS.id()), strNewName.c_str(), nNewAge,
             strNewDesc.size(), strNewAvatar.size(),
             stS.accountId() > 0 ? ", persisted" : ", session-only");
}

/**
 * @brief      执行 onping；实现上向目标会话或整个房间下发数据。
 * @param[out] stS            目标会话对象；类型 CSession&。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CUserService::onPing(CSession& stS, const Json& /*j*/) {
    Json stResp = Json::object();
    stResp["ts"] = nowMs();
    m_stCtx.sendTo(stS, OpCode_EN::Pong, stResp);
}

/**
 * @brief  namespace dp。
 */
}
