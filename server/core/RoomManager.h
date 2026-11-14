/**
 * @file       RoomManager.h
 * @brief      房间容器：按房间号索引，负责创建、查找与回收。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#pragma once

#include <map>
#include <memory>
#include <string>

#include "server/core/Room.h"

namespace dp {

/**
 * @brief   房间表。
 * @details 只在事件循环线程使用，不加锁。 空房不立即销毁，而是由 CRoom 自己标记滞留，超时后由 CReactor::handleTimer() 调用 `removeIfEmpty()` 回收。
 * @author  JaTin
 */
class CRoomManager {
public:
    CRoomManager() = default;
    CRoomManager(const CRoomManager&) = delete;
    CRoomManager& operator=(const CRoomManager&) = delete;

    /**
     * @brief      取房间，不存在则创建。
     * @param[in] id             房间号；类型 const std::string&。
     * @return     房间引用；在本房间被销毁前一直有效。
     * @author     JaTin
     */
    CRoom& getOrCreate(const std::string& id);

    /**
     * @brief      查找房间。
     * @param[in] id             房间号；类型 const std::string&。
     * @return     房间指针；不存在时返回 nullptr。
     * @author     JaTin
     */
    CRoom* find(const std::string& id);

    /**
     * @brief      房间已空则销毁。
     * @details    非空房间不会被销毁，防止误删正在使用的房间。
     * @param[in] id             房间号；类型 const std::string&。
     * @return     true  房间存在且为空，已销毁；false 房间不存在，或仍有成员。
     * @author     JaTin
     */
    bool removeIfEmpty(const std::string& id);

    /**
     * @brief  当前房间数（含滞留中的空房）。
     */
    size_t size() const { return rooms_.size(); }

    /**
     * @brief  全部房间（按房间号有序），仅供遍历与统计。
     */
    const std::map<std::string, std::unique_ptr<CRoom>>& all() const { return rooms_; }

private:
    /**
     * @brief  房间号 -> 房间
     * @note   类型 std::map<std::string, std::unique_ptr<CRoom>>；类/结构体成员变量。
     */
    std::map<std::string, std::unique_ptr<CRoom>> rooms_;
};

/**
 * @brief  namespace dp。
 */
}
