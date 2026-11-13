/**
 * @file  ServiceRegistry.h
 * @brief opcode 到业务服务的注册表（进程级单例）。
 * @details 让 Reactor 的分发逻辑与具体业务完全解耦：新增一种请求只需要
 *          写一个 Service 派生类并注册，事件循环一行都不用改。
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
class ServiceRegistry {
public:
    /**
     * @brief 获取全局唯一实例。
     * @retval 单例引用，首次调用时构造。
     */
    static ServiceRegistry& instance();

    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    /**
     * @brief 注册一个服务，接管其所有权。
     * @param service 服务实例，不可为空。
     * @details 若某个 opcode 已被占用，会打印错误并整体拒绝注册
     *          （而不是部分注册），以免出现难以排查的分发歧义。
     */
    void registerService(std::unique_ptr<Service> service);

    /**
     * @brief 按 opcode 分发一条请求。
     * @param session 发起请求的会话。
     * @param op      请求类型。
     * @param payload 已解析的 JSON 负载。
     * @retval true  已找到对应服务并完成处理。
     * @retval false 没有服务注册该 opcode，调用方应回 `unknown_op`。
     */
    bool dispatch(Session& session, OpCode op, const Json& payload);

    /// @brief 清空所有注册（仅用于测试或进程退出前收尾）。
    void clear();

    /// @brief 已注册的服务实例数量。
    size_t size() const noexcept { return owned_.size(); }

private:
    ServiceRegistry() = default;
    ~ServiceRegistry() = default;

    std::vector<std::unique_ptr<Service>> owned_;   ///< 服务实例的唯一所有者
    std::map<OpCode, Service*>            index_;   ///< opcode -> 服务（不持有）
};

}  // namespace dp
