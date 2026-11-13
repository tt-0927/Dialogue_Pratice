/*
 * 文件名:    RoomManager.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  CRoomManager 头文件，声明相关类型与接口。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

/* server/CRoomManager.h */
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "server/Room.h"

namespace dp {

class CRoomManager {
public:
    CRoomManager() = default;
    CRoomManager(const CRoomManager&) = delete;
    CRoomManager& operator=(const CRoomManager&) = delete;

    /* 不存在则创建。返回引用在房间销毁前有效。 */
    CRoom& getOrCreate(const std::string& id);
    CRoom* find(const std::string& id);

    /* 房间已空则销毁，返回是否销毁。 */
    bool removeIfEmpty(const std::string& id);

    size_t size() const { return rooms_.size(); }
    const std::map<std::string, std::unique_ptr<CRoom>>& all() const { return rooms_; }

private:
    std::map<std::string, std::unique_ptr<CRoom>> rooms_;
};

/* namespace dp */
}
