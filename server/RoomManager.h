/**
 * @file       RoomManager.h
 * @brief      CRoomManager 头文件，声明相关类型与接口。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

/**
 * @brief  server/CRoomManager.h。
 */
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "server/Room.h"

namespace dp {

/**
 * @brief   C房间管理器。
 * @author  JaTin
 */
class CRoomManager {
public:
    CRoomManager() = default;
    CRoomManager(const CRoomManager&) = delete;
    CRoomManager& operator=(const CRoomManager&) = delete;

    /**
     * @brief      不存在则创建。返回引用在房间销毁前有效。
     * @param[in] id             编号；类型 const std::string&。
     * @return     返回 CRoom& 类型的对象引用或指针，调用方按接口约定使用。
     * @author     JaTin
     */
    CRoom& getOrCreate(const std::string& id);
    /**
     * @brief      查找 find。
     * @param[in] id             编号；类型 const std::string&。
     * @return     成功返回对象指针；未找到或失败时返回 nullptr。
     * @author     JaTin
     */
    CRoom* find(const std::string& id);

    /**
     * @brief      房间已空则销毁，返回是否销毁。
     * @param[in] id             编号；类型 const std::string&。
     * @return     成功返回 true；参数非法或执行失败时返回 false。
     * @author     JaTin
     */
    bool removeIfEmpty(const std::string& id);

    size_t size() const { return rooms_.size(); }
    const std::map<std::string, std::unique_ptr<CRoom>>& all() const { return rooms_; }

private:
    /**
     * @brief  房间列表。
     * @note   类型 std::map<std::string, std::unique_ptr<CRoom>>；类/结构体成员变量。
     */
    std::map<std::string, std::unique_ptr<CRoom>> rooms_;
};

/**
 * @brief  namespace dp。
 */
}
