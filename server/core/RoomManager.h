/*
 * 文件名:    RoomManager.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  房间容器：按房间号索引，负责创建、查找与回收。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
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
 *          空房不立即销毁，而是由 CRoom 自己标记滞留，超时后由
 *          CReactor::handleTimer() 调用 `removeIfEmpty()` 回收。
 */
class CRoomManager {
public:
    CRoomManager() = default;
    CRoomManager(const CRoomManager&) = delete;
    CRoomManager& operator=(const CRoomManager&) = delete;

    /**
     * @brief 取房间，不存在则创建。
     * @param id 房间号。
     * @retval 房间引用；在本房间被销毁前一直有效。
     */
    CRoom& getOrCreate(const std::string& id);

    /**
     * @brief 查找房间。
     * @param id 房间号。
     * @retval 房间指针；不存在时返回 nullptr。
     */
    CRoom* find(const std::string& id);

    /**
     * @brief 房间已空则销毁。
     * @param id 房间号。
     * @retval true  房间存在且为空，已销毁。
     * @retval false 房间不存在，或仍有成员。
     * @details 非空房间不会被销毁，防止误删正在使用的房间。
     */
    bool removeIfEmpty(const std::string& id);

    /* 当前房间数（含滞留中的空房）。 */
    size_t size() const { return rooms_.size(); }

    /* 全部房间（按房间号有序），仅供遍历与统计。 */
    const std::map<std::string, std::unique_ptr<CRoom>>& all() const { return rooms_; }

private:
    /* < 房间号 -> 房间 */
    std::map<std::string, std::unique_ptr<CRoom>> rooms_;
};

/* namespace dp */
}
