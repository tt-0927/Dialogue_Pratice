/**
 * @file  RoomManager.h
 * @brief 房间容器：按房间号索引，负责创建、查找与回收。
 * @details 所有权集中：所有 Room 由本类的 `std::map<string, unique_ptr>` 独占持有，
 *          外部只拿裸指针或引用观察。
 *
 *          房间无需预建——`getOrCreate()` 遇到不存在的房间号就直接创建，
 *          这与协议层「进房即建」的语义一致。
 */

#pragma once

#include <map>
#include <memory>
#include <string>

#include "server/core/Room.h"

namespace dp {

/**
 * @brief 房间表。
 *
 * @details 只在事件循环线程使用，不加锁。
 *          空房不立即销毁，而是由 Room 自己标记滞留，超时后由
 *          Reactor::handleTimer() 调用 `removeIfEmpty()` 回收。
 */
class RoomManager {
public:
    RoomManager() = default;
    RoomManager(const RoomManager&) = delete;
    RoomManager& operator=(const RoomManager&) = delete;

    /**
     * @brief 取房间，不存在则创建。
     * @param id 房间号。
     * @retval 房间引用；在本房间被销毁前一直有效。
     */
    Room& getOrCreate(const std::string& id);

    /**
     * @brief 查找房间。
     * @param id 房间号。
     * @retval 房间指针；不存在时返回 nullptr。
     */
    Room* find(const std::string& id);

    /**
     * @brief 房间已空则销毁。
     * @param id 房间号。
     * @retval true  房间存在且为空，已销毁。
     * @retval false 房间不存在，或仍有成员。
     * @details 非空房间不会被销毁，防止误删正在使用的房间。
     */
    bool removeIfEmpty(const std::string& id);

    /// @brief 当前房间数（含滞留中的空房）。
    size_t size() const { return rooms_.size(); }

    /// @brief 全部房间（按房间号有序），仅供遍历与统计。
    const std::map<std::string, std::unique_ptr<Room>>& all() const { return rooms_; }

private:
    std::map<std::string, std::unique_ptr<Room>> rooms_;   ///< 房间号 -> 房间
};

}  // namespace dp
