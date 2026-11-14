/**
 * @file       SessionManager.h
 * @brief      会话容器：负责 id 分配与生命周期。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "server/core/Session.h"

namespace dp {

/**
 * @brief   会话表。
 * @details 用 `std::map` 而非 `unordered_map`：会话数在数千量级， 有序遍历在日志与统计时更友好，插入开销可以忽略。 只在事件循环线程使用，不加锁。
 * @author  JaTin
 */
class CSessionManager {
public:
    CSessionManager() = default;
    CSessionManager(const CSessionManager&) = delete;
    CSessionManager& operator=(const CSessionManager&) = delete;

    /**
     * @brief      创建并接管一条连接。
     * @param[in] fd             非阻塞 socket，所有权转移给新建的 CSession；类型 int。
     * @param[in] peer           对端地址描述（日志用）；类型 std::string。
     * @return     新建会话的非空指针；id 由内部单调递增分配。
     * @author     JaTin
     */
    CSession* add(int fd, std::string peer);

    /**
     * @brief      按 id 查找会话。
     * @param[in] id             会话 id；类型 uint64_t。
     * @return     会话指针；不存在时返回 nullptr。
     * @author     JaTin
     */
    CSession* find(uint64_t id);

    /**
     * @brief      销毁会话（析构中关闭 fd）。
     * @param[in] id             会话 id；类型 uint64_t。
     * @return     true  找到并销毁；false id 不存在。
     * @author     JaTin
     */
    bool remove(uint64_t id);

    /**
     * @brief  当前连接数。
     */
    size_t size() const { return m_uStore.size(); }

    /**
     * @brief  全部会话（按 id 有序），仅供遍历与统计，不可增删。
     */
    const std::map<uint64_t, std::unique_ptr<CSession>>& all() const { return m_uStore; }

private:
    /**
     * @brief  id -> 会话
     * @note   类型 std::map<uint64_t, std::unique_ptr<CSession>>；类/结构体成员变量。
     */
    std::map<uint64_t, std::unique_ptr<CSession>> m_uStore;
    /**
     * @brief  下一个可用 id
     * @note   类型 uint64_t；类/结构体成员变量。
     */
    uint64_t m_uNextId = 1;
};

/**
 * @brief  namespace dp。
 */
}
