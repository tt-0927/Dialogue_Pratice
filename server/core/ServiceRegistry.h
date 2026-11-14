/**
 * @file       ServiceRegistry.h
 * @brief      opcode 到业务服务的注册表（进程级单例）。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#pragma once

#include <map>
#include <memory>
#include <vector>

#include "common/Protocol.h"
#include "server/core/Service.h"

namespace dp {

/**
 * @brief   业务服务的注册与分发中心。
 * @details 单例由函数内静态对象实现，C++11 起保证线程安全的一次性初始化。 服务在服务端启动时一次性注册，运行期只读，因此分发路径上没有锁开销。 这样「一个服务对应多个 opcode」时不必对同一个 unique_ptr 多次 move。
 * @author  JaTin
 */
class CServiceRegistry {
public:
    /**
     * @brief      获取全局唯一实例。
     * @note       本函数无输入参数。
     * @return     单例引用，首次调用时构造。
     * @author     JaTin
     */
    static CServiceRegistry& instance();

    CServiceRegistry(const CServiceRegistry&) = delete;
    CServiceRegistry& operator=(const CServiceRegistry&) = delete;

    /**
     * @brief      注册一个服务，接管其所有权。
     * @details    若某个 opcode 已被占用，会打印错误并整体拒绝注册 （而不是部分注册），以免出现难以排查的分发歧义。
     * @param[in] service        服务实例，不可为空；类型 std::unique_ptr<CService>。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void registerService(std::unique_ptr<CService> service);

    /**
     * @brief      按 opcode 分发一条请求。
     * @param[out] session        发起请求的会话；类型 CSession&。
     * @param[in] op             请求类型；类型 OpCode_EN。
     * @param[in] payload        已解析的 JSON 负载；类型 const Json&。
     * @return     true  已找到对应服务并完成处理；false 没有服务注册该 opcode，调用方应回 `unknown_op`。
     * @author     JaTin
     */
    bool dispatch(CSession& session, OpCode_EN op, const Json& payload);

    /**
     * @brief      清空所有注册（仅用于测试或进程退出前收尾）。
     * @note       本函数无输入参数。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void clear();

    /**
     * @brief  已注册的服务实例数量。
     */
    size_t size() const noexcept { return m_aOwned.size(); }

private:
    CServiceRegistry() = default;
    ~CServiceRegistry() = default;

    /**
     * @brief  服务实例的唯一所有者
     * @note   类型 std::vector<std::unique_ptr<CService>>；类/结构体成员变量。
     */
    std::vector<std::unique_ptr<CService>> m_aOwned;
    /**
     * @brief  opcode -> 服务（不持有）
     * @note   类型 std::map<OpCode_EN, CService*>；类/结构体成员变量。
     */
    std::map<OpCode_EN, CService*>            m_aIndex;
};

/**
 * @brief  namespace dp。
 */
}
