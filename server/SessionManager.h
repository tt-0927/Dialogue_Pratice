/*
 * 文件名:    SessionManager.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  CSessionManager 头文件，声明相关类型与接口。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

/* server/CSessionManager.h */

/* CSession 的容器：负责 id 分配与生命周期。 */
/* 注意：fd 的关闭由 CSession 析构完成，CServer 需先 epoll_ctl(DEL) 再 remove()。 */
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "server/Session.h"

namespace dp {

class CSessionManager {
public:
    CSessionManager() = default;
    CSessionManager(const CSessionManager&) = delete;
    CSessionManager& operator=(const CSessionManager&) = delete;

    /* 创建并接管一条连接，返回非空指针（id 由内部单调递增分配）。 */
    CSession* add(int fd, std::string peer);

    CSession* find(uint64_t id);

    /* 销毁 session（析构中关闭 fd）。返回是否找到。 */
    bool remove(uint64_t id);

    size_t size() const { return m_uStore.size(); }

    const std::map<uint64_t, std::unique_ptr<CSession>>& all() const { return m_uStore; }

private:
    std::map<uint64_t, std::unique_ptr<CSession>> m_uStore;
    uint64_t m_uNextId = 1;
};

/* namespace dp */
}
