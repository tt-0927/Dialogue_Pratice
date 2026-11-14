/**
 * @file       SessionManager.h
 * @brief      CSessionManager 头文件，声明相关类型与接口。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

/**
 * @brief  server/CSessionManager.h。
 */

/**
 * @brief  CSession 的容器：负责 id 分配与生命周期。 注意：fd 的关闭由 CSession 析构完成，CServer 需先 epoll_ctl(DEL) 再 remove()。
 */
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "server/Session.h"

namespace dp {

/**
 * @brief   C会话管理器。
 * @author  JaTin
 */
class CSessionManager {
public:
    CSessionManager() = default;
    CSessionManager(const CSessionManager&) = delete;
    CSessionManager& operator=(const CSessionManager&) = delete;

    /**
     * @brief      创建并接管一条连接，返回非空指针（id 由内部单调递增分配）。
     * @param[in] fd             文件描述符；类型 int。
     * @param[in] peer           输入参数；类型 std::string。
     * @return     成功返回对象指针；未找到或失败时返回 nullptr。
     * @author     JaTin
     */
    CSession* add(int fd, std::string peer);

    /**
     * @brief      查找 find。
     * @param[in] id             编号；类型 uint64_t。
     * @return     成功返回对象指针；未找到或失败时返回 nullptr。
     * @author     JaTin
     */
    CSession* find(uint64_t id);

    /**
     * @brief      销毁 session（析构中关闭 fd）。返回是否找到。
     * @param[in] id             编号；类型 uint64_t。
     * @return     成功返回 true；参数非法或执行失败时返回 false。
     * @author     JaTin
     */
    bool remove(uint64_t id);

    size_t size() const { return m_uStore.size(); }

    const std::map<uint64_t, std::unique_ptr<CSession>>& all() const { return m_uStore; }

private:
    /**
     * @brief  存储。
     * @note   类型 std::map<uint64_t, std::unique_ptr<CSession>>；类/结构体成员变量。
     */
    std::map<uint64_t, std::unique_ptr<CSession>> m_uStore;
    /**
     * @brief  Next编号。 初值 = 1。
     * @note   类型 uint64_t；类/结构体成员变量。
     */
    uint64_t m_uNextId = 1;
};

/**
 * @brief  namespace dp。
 */
}
