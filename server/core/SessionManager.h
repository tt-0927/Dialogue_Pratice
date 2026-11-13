/*
 * 文件名:    SessionManager.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  会话容器：负责 id 分配与生命周期。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "server/core/Session.h"

namespace dp {

/**
 * @brief 会话表。
 *
 * @details 用 `std::map` 而非 `unordered_map`：会话数在数千量级，
 *          有序遍历在日志与统计时更友好，插入开销可以忽略。
 *          只在事件循环线程使用，不加锁。
 */
class CSessionManager {
public:
    CSessionManager() = default;
    CSessionManager(const CSessionManager&) = delete;
    CSessionManager& operator=(const CSessionManager&) = delete;

    /**
     * @brief 创建并接管一条连接。
     * @param fd   非阻塞 socket，所有权转移给新建的 CSession。
     * @param peer 对端地址描述（日志用）。
     * @retval 新建会话的非空指针；id 由内部单调递增分配。
     */
    CSession* add(int fd, std::string peer);

    /**
     * @brief 按 id 查找会话。
     * @param id 会话 id。
     * @retval 会话指针；不存在时返回 nullptr。
     */
    CSession* find(uint64_t id);

    /**
     * @brief 销毁会话（析构中关闭 fd）。
     * @param id 会话 id。
     * @retval true  找到并销毁。
     * @retval false id 不存在。
     */
    bool remove(uint64_t id);

    /* 当前连接数。 */
    size_t size() const { return m_uStore.size(); }

    /* 全部会话（按 id 有序），仅供遍历与统计，不可增删。 */
    const std::map<uint64_t, std::unique_ptr<CSession>>& all() const { return m_uStore; }

private:
    /* < id -> 会话 */
    std::map<uint64_t, std::unique_ptr<CSession>> m_uStore;
    /* < 下一个可用 id */
    uint64_t m_uNextId = 1;
};

/* namespace dp */
}
