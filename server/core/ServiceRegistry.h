/*
 * 文件名:    ServiceRegistry.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  opcode 到业务服务的注册表（进程级单例）。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#pragma once

#include <map>
#include <memory>
#include <vector>

#include "common/Protocol.h"
#include "server/core/Service.h"

namespace dp {

/**
 * @brief 业务服务的注册与分发中心。
 *
 * @details 单例由函数内静态对象实现，C++11 起保证线程安全的一次性初始化。
 *          服务在服务端启动时一次性注册，运行期只读，因此分发路径上没有锁开销。
 *
 * @note 所有权与索引分离：`owned_` 独占持有服务实例，`index_` 只存裸指针。
 *       这样「一个服务对应多个 opcode」时不必对同一个 unique_ptr 多次 move。
 */
class CServiceRegistry {
public:
    /**
     * @brief 获取全局唯一实例。
     * @retval 单例引用，首次调用时构造。
     */
    static CServiceRegistry& instance();

    CServiceRegistry(const CServiceRegistry&) = delete;
    CServiceRegistry& operator=(const CServiceRegistry&) = delete;

    /**
     * @brief 注册一个服务，接管其所有权。
     * @param service 服务实例，不可为空。
     * @details 若某个 opcode 已被占用，会打印错误并整体拒绝注册
     *          （而不是部分注册），以免出现难以排查的分发歧义。
     */
    void registerService(std::unique_ptr<CService> service);

    /**
     * @brief 按 opcode 分发一条请求。
     * @param session 发起请求的会话。
     * @param op      请求类型。
     * @param payload 已解析的 JSON 负载。
     * @retval true  已找到对应服务并完成处理。
     * @retval false 没有服务注册该 opcode，调用方应回 `unknown_op`。
     */
    bool dispatch(CSession& session, OpCode_EN op, const Json& payload);

    /* 清空所有注册（仅用于测试或进程退出前收尾）。 */
    void clear();

    /* 已注册的服务实例数量。 */
    size_t size() const noexcept { return m_aOwned.size(); }

private:
    CServiceRegistry() = default;
    ~CServiceRegistry() = default;

    /* < 服务实例的唯一所有者 */
    std::vector<std::unique_ptr<CService>> m_aOwned;
    /* < opcode -> 服务（不持有） */
    std::map<OpCode_EN, CService*>            m_aIndex;
};

/* namespace dp */
}
