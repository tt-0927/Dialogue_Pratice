// server/RoomManager.cpp
#include "server/core/RoomManager.h"

namespace dp {

Room& RoomManager::getOrCreate(const std::string& id) {
    auto it = rooms_.find(id);
    if (it != rooms_.end()) return *it->second;

    auto room = std::make_unique<Room>(id);
    Room* raw = room.get();
    rooms_.emplace(id, std::move(room));
    return *raw;
}

Room* RoomManager::find(const std::string& id) {
    auto it = rooms_.find(id);
    return it == rooms_.end() ? nullptr : it->second.get();
}

bool RoomManager::removeIfEmpty(const std::string& id) {
    auto it = rooms_.find(id);
    if (it == rooms_.end()) return false;
    if (!it->second->empty()) return false;
    rooms_.erase(it);
    return true;
}

}  // namespace dp
