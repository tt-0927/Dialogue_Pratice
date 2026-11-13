// server/SessionManager.cpp
#include "server/core/SessionManager.h"

#include <utility>

namespace dp {

Session* SessionManager::add(int fd, std::string peer) {
    const uint64_t id = nextId_++;
    auto session = std::make_unique<Session>(fd, id, std::move(peer));
    Session* raw = session.get();
    store_.emplace(id, std::move(session));
    return raw;
}

Session* SessionManager::find(uint64_t id) {
    auto it = store_.find(id);
    return it == store_.end() ? nullptr : it->second.get();
}

bool SessionManager::remove(uint64_t id) {
    return store_.erase(id) > 0;
}

}  // namespace dp
