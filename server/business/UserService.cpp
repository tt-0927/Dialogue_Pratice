/**
 * @file  UserService.cpp
 * @brief 用户领域业务的实现。
 */

#include "server/business/UserService.h"

#include <optional>

#include "server/core/Logger.h"
#include "server/core/TimeUtil.h"
#include "server/core/Validation.h"

namespace dp {
namespace {

/**
 * @brief 构造下发给客户端的资料快照。
 * @param s 会话。
 * @retval 含 userId / name / age / desc 的 JSON 对象。
 */
Json profileJson(const Session& s) {
    Json p = Json::object();
    p["userId"] = s.id();
    p["name"] = s.name();
    p["age"] = s.age();
    p["desc"] = s.desc();
    return p;
}

}  // namespace

std::vector<OpCode> UserService::opcodes() const {
    return {OpCode::Login, OpCode::Register, OpCode::UpdateProfile, OpCode::Ping};
}

void UserService::handle(Session& session, OpCode op, const Json& payload) {
    switch (op) {
        case OpCode::Login:         onLogin(session, payload);         break;
        case OpCode::Register:      onRegister(session, payload);      break;
        case OpCode::UpdateProfile: onUpdateProfile(session, payload); break;
        case OpCode::Ping:          onPing(session, payload);          break;
        default:                                                       break;
    }
}

void UserService::onLogin(Session& s, const Json& j) {
    if (s.loggedIn()) {
        ctx_.sendError(s, "already_logged_in", "session already logged in");
        return;
    }
    if (!j.contains("name") || !j["name"].is_string()) {
        ctx_.sendError(s, "bad_payload", "field 'name' (string) is required");
        return;
    }
    const std::string name = trim(j["name"].get<std::string>());
    if (!validName(name)) {
        ctx_.sendError(s, "bad_name", "name must be 1..24 bytes without control chars");
        return;
    }

    int age = 0;
    if (j.contains("age") && !j["age"].is_null()) {
        if (!readAge(j, age)) {
            ctx_.sendError(s, "bad_age", "age must be an integer in 0..150");
            return;
        }
    }

    std::string desc;
    if (j.contains("desc") && !j["desc"].is_null()) {
        if (!j["desc"].is_string()) {
            ctx_.sendError(s, "bad_payload", "field 'desc' must be a string");
            return;
        }
        desc = trim(j["desc"].get<std::string>());
        if (!validDesc(desc)) {
            ctx_.sendError(s, "bad_desc", "desc must be <= 200 bytes");
            return;
        }
    }

    // 客户端带了 password 就走账号校验；没带则走兼容路径（昵称匹配或匿名）。
    const bool hasPassword = j.contains("password") && j["password"].is_string()
                             && !j["password"].get<std::string>().empty();

    int64_t accountId = 0;
    std::optional<UserRecord> user;

    if (hasPassword) {
        UserRecord verified;
        if (!ctx_.users().verifyCredentials(name, j["password"].get<std::string>(),
                                            &verified)) {
            // 统一提示，不区分「账号不存在」和「密码错误」，避免账号枚举
            LOG_WARN("session#%llu 登录失败（账号 '%s'）",
                     static_cast<unsigned long long>(s.id()), name.c_str());
            ctx_.sendError(s, "auth_failed", "账号或密码错误");
            return;
        }
        user = verified;
    } else if (ctx_.config().requireAuth) {
        ctx_.sendError(s, "auth_required", "本服务器要求账号密码登录");
        return;
    } else {
        // 兼容模式：先按登录名精确匹配，再按昵称忽略大小写匹配
        user = ctx_.users().findByUsername(name);
        if (!user) user = ctx_.users().findByNickname(name);
    }

    if (user) {
        accountId = user->id;
        s.setName(user->nickname);
        s.setAge(user->age);
        s.setDesc(user->description);
        ctx_.users().touchLogin(user->id);
        LOG_INFO("session#%llu 匹配到账号 #%lld（%s，%s）",
                 static_cast<unsigned long long>(s.id()),
                 static_cast<long long>(user->id), user->username.c_str(),
                 hasPassword ? "密码校验" : "昵称匹配");
    } else {
        s.setName(name);
        s.setAge(age);
        s.setDesc(desc);
        LOG_INFO("session#%llu 匿名登录（昵称 '%s' 不在用户库）",
                 static_cast<unsigned long long>(s.id()), name.c_str());
    }
    s.setAccountId(accountId);
    s.setState(SessionState::LoggedIn);

    Json resp = profileJson(s);
    resp["ok"] = true;
    resp["serverTime"] = nowMs();
    resp["accountId"] = accountId;   // 0 表示匿名；客户端可用于区分身份来源

    // 服务端统一配置：客户端不必各自维护 llm.json。
    // 结构原样透传（由客户端的 AppConfig 解析），服务端不关心字段含义。
    if (ctx_.hasLlmConfig()) {
        resp["llmConfig"] = ctx_.llmConfig();
    }

    ctx_.sendTo(s, OpCode::LoginResp, resp);

    Json welcome = Json::object();
    welcome["event"] = "welcome";
    welcome["text"] = "欢迎 " + name + "，请用房间号加入房间";
    welcome["ts"] = nowMs();
    ctx_.sendTo(s, OpCode::SystemMsg, welcome);

    LOG_INFO("session#%llu login as '%s'",
             static_cast<unsigned long long>(s.id()), name.c_str());
}

void UserService::onRegister(Session& s, const Json& j) {
    // 单连接注册次数上限：防止有人写个脚本批量刷号
    constexpr int kMaxRegisterPerSession = 3;

    if (!ctx_.config().allowRegistration) {
        ctx_.sendError(s, "register_disabled", "本服务器不开放自助注册");
        return;
    }
    if (s.loggedIn()) {
        ctx_.sendError(s, "already_logged_in", "已登录，无需注册");
        return;
    }
    if (s.registerAttempts() >= kMaxRegisterPerSession) {
        ctx_.sendError(s, "too_many_registrations", "本连接注册次数过多，请重新连接");
        return;
    }
    s.bumpRegisterAttempts();

    // ---- 必填：登录名 ----
    if (!j.contains("username") || !j["username"].is_string()) {
        ctx_.sendError(s, "bad_payload", "field 'username' (string) is required");
        return;
    }
    const std::string username = trim(j["username"].get<std::string>());
    if (!validUsername(username)) {
        ctx_.sendError(s, "bad_username",
                       "登录名需为 3..24 位的字母、数字、下划线或短横线");
        return;
    }

    // ---- 必填：密码 ----
    if (!j.contains("password") || !j["password"].is_string()) {
        ctx_.sendError(s, "bad_payload", "field 'password' (string) is required");
        return;
    }
    const std::string password = j["password"].get<std::string>();
    if (!validPassword(password)) {
        ctx_.sendError(s, "weak_password", "密码长度需为 6..64 位");
        return;
    }

    // ---- 可选：昵称（缺省用登录名）----
    std::string nickname = username;
    if (j.contains("nickname") && j["nickname"].is_string()) {
        nickname = trim(j["nickname"].get<std::string>());
        if (!validName(nickname)) {
            ctx_.sendError(s, "bad_name", "昵称需为 1..24 字节且不含控制字符");
            return;
        }
    }

    // ---- 可选：年龄 ----
    int age = 0;
    if (j.contains("age") && !j["age"].is_null() && !readAge(j, age)) {
        ctx_.sendError(s, "bad_age", "age must be an integer in 0..150");
        return;
    }

    // ---- 可选：简介 ----
    std::string desc;
    if (j.contains("desc") && j["desc"].is_string()) {
        desc = trim(j["desc"].get<std::string>());
        if (!validDesc(desc)) {
            ctx_.sendError(s, "bad_desc", "desc must be <= 200 bytes");
            return;
        }
    }

    // ---- 唯一性检查 ----
    if (ctx_.users().findByUsername(username)) {
        ctx_.sendError(s, "username_taken", "该登录名已被占用");
        return;
    }

    std::string error;
    if (!ctx_.users().createUser(username, password, nickname, age, desc, &error)) {
        LOG_ERROR("注册失败（%s）：%s", username.c_str(), error.c_str());
        ctx_.sendError(s, "register_failed", "注册失败，请稍后再试");
        return;
    }

    Json resp = Json::object();
    resp["ok"] = true;
    resp["username"] = username;
    resp["nickname"] = nickname;
    if (auto created = ctx_.users().findByUsername(username)) {
        resp["userId"] = created->id;
    }
    ctx_.sendTo(s, OpCode::RegisterResp, resp);

    LOG_INFO("session#%llu 注册新账号 '%s'（昵称 '%s'，年龄 %d）",
             static_cast<unsigned long long>(s.id()), username.c_str(),
             nickname.c_str(), age);
}

void UserService::onUpdateProfile(Session& s, const Json& j) {
    if (!s.loggedIn()) {
        ctx_.sendError(s, "not_logged_in", "login first");
        return;
    }

    // 只更新请求里出现的字段，没带的保持原值
    std::string newName = s.name();
    if (j.contains("name") && !j["name"].is_null()) {
        if (!j["name"].is_string()) {
            ctx_.sendError(s, "bad_payload", "field 'name' must be a string");
            return;
        }
        newName = trim(j["name"].get<std::string>());
        if (!validName(newName)) {
            ctx_.sendError(s, "bad_name", "name must be 1..24 bytes without control chars");
            return;
        }
    }

    int newAge = s.age();
    if (j.contains("age") && !j["age"].is_null()) {
        if (!readAge(j, newAge)) {
            ctx_.sendError(s, "bad_age", "age must be an integer in 0..150");
            return;
        }
    }

    std::string newDesc = s.desc();
    if (j.contains("desc") && !j["desc"].is_null()) {
        if (!j["desc"].is_string()) {
            ctx_.sendError(s, "bad_payload", "field 'desc' must be a string");
            return;
        }
        newDesc = trim(j["desc"].get<std::string>());
        if (!validDesc(newDesc)) {
            ctx_.sendError(s, "bad_desc", "desc must be <= 200 bytes");
            return;
        }
    }

    const std::string oldName = s.name();
    const bool renamed = (newName != oldName);

    s.setName(newName);
    s.setAge(newAge);
    s.setDesc(newDesc);

    Json resp = profileJson(s);
    resp["ok"] = true;
    ctx_.sendTo(s, OpCode::ProfileResp, resp);

    // 改名必须通知同房间，否则别人成员列表里还是旧名字
    if (renamed && !s.room().empty()) {
        Json sys = Json::object();
        sys["event"] = "rename";
        sys["room"] = s.room();
        sys["name"] = newName;          // 客户端用它更新成员列表
        sys["text"] = oldName + " 改名为 " + newName;
        sys["ts"] = nowMs();
        ctx_.broadcastRoom(s.room(), OpCode::SystemMsg, sys, 0);
    }

    LOG_INFO("session#%llu profile updated (name='%s', age=%d, desc=%zu B)",
             static_cast<unsigned long long>(s.id()), newName.c_str(), newAge, newDesc.size());
}

void UserService::onPing(Session& s, const Json& /*j*/) {
    Json resp = Json::object();
    resp["ts"] = nowMs();
    ctx_.sendTo(s, OpCode::Pong, resp);
}

}  // namespace dp
