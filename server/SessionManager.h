// server/SessionManager.h
//
// Session 的容器：负责 id 分配与生命周期。
// 注意：fd 的关闭由 Session 析构完成，Server 需先 epoll_ctl(DEL) 再 remove()。
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "server/Session.h"

namespace dp {

class SessionManager {
public:
    SessionManager() = default;
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    /// 创建并接管一条连接，返回非空指针（id 由内部单调递增分配）。
    Session* add(int fd, std::string peer);

    Session* find(uint64_t id);

    /// 销毁 session（析构中关闭 fd）。返回是否找到。
    bool remove(uint64_t id);

    size_t size() const { return store_.size(); }

    const std::map<uint64_t, std::unique_ptr<Session>>& all() const { return store_; }

private:
    std::map<uint64_t, std::unique_ptr<Session>> store_;
    uint64_t nextId_ = 1;
};

}  // namespace dp
