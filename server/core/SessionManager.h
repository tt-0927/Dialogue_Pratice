/**
 * @file  SessionManager.h
 * @brief 会话容器：负责 id 分配与生命周期。
 * @details 所有权集中：所有 Session 由本类的 `std::map<uint64_t, unique_ptr>` 独占持有，
 *          外部只拿裸指针观察，从不负责释放。
 *
 * @note fd 的关闭由 Session 析构完成，因此调用方必须先 `epoll_ctl(DEL)` 再 `remove()`。
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
class SessionManager {
public:
    SessionManager() = default;
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    /**
     * @brief 创建并接管一条连接。
     * @param fd   非阻塞 socket，所有权转移给新建的 Session。
     * @param peer 对端地址描述（日志用）。
     * @retval 新建会话的非空指针；id 由内部单调递增分配。
     */
    Session* add(int fd, std::string peer);

    /**
     * @brief 按 id 查找会话。
     * @param id 会话 id。
     * @retval 会话指针；不存在时返回 nullptr。
     */
    Session* find(uint64_t id);

    /**
     * @brief 销毁会话（析构中关闭 fd）。
     * @param id 会话 id。
     * @retval true  找到并销毁。
     * @retval false id 不存在。
     */
    bool remove(uint64_t id);

    /// @brief 当前连接数。
    size_t size() const { return store_.size(); }

    /// @brief 全部会话（按 id 有序），仅供遍历与统计，不可增删。
    const std::map<uint64_t, std::unique_ptr<Session>>& all() const { return store_; }

private:
    std::map<uint64_t, std::unique_ptr<Session>> store_;   ///< id -> 会话
    uint64_t nextId_ = 1;                                  ///< 下一个可用 id
};

}  // namespace dp
