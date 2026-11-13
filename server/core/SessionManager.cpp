/*
 * 文件名:    SessionManager.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  CSessionManager 实现文件，包含相关接口的具体实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

/* server/CSessionManager.cpp */
#include "server/core/SessionManager.h"

#include <utility>

namespace dp {

/*
 * 功能描述:  执行 add 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  fd  参数。
 * param [in]  peer  参数。
 * return:    返回值，含义见功能描述。
 */
CSession* CSessionManager::add(int fd, std::string peer) {
    const uint64_t id = m_uNextId++;
    auto session = std::make_unique<CSession>(fd, id, std::move(peer));
    CSession* raw = session.get();
    m_uStore.emplace(id, std::move(session));
    return raw;
}

/*
 * 功能描述:  执行 find 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  id  参数。
 * return:    返回值，含义见功能描述。
 */
CSession* CSessionManager::find(uint64_t id) {
    auto it = m_uStore.find(id);
    return it == m_uStore.end() ? nullptr : it->second.get();
}

/*
 * 功能描述:  执行 remove 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  id  参数。
 * return:    返回值，含义见功能描述。
 */
bool CSessionManager::remove(uint64_t id) {
    return m_uStore.erase(id) > 0;
}

/* namespace dp */
}
