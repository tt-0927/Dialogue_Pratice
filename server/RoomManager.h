// server/RoomManager.h
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "server/Room.h"

namespace dp {

class RoomManager {
public:
    RoomManager() = default;
    RoomManager(const RoomManager&) = delete;
    RoomManager& operator=(const RoomManager&) = delete;

    /// 不存在则创建。返回引用在房间销毁前有效。
    Room& getOrCreate(const std::string& id);
    Room* find(const std::string& id);

    /// 房间已空则销毁，返回是否销毁。
    bool removeIfEmpty(const std::string& id);

    size_t size() const { return rooms_.size(); }
    const std::map<std::string, std::unique_ptr<Room>>& all() const { return rooms_; }

private:
    std::map<std::string, std::unique_ptr<Room>> rooms_;
};

}  // namespace dp
