/**
 * @file       MessageRepository.h
 * @brief      聊天记录的写入与查询。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释；
 *             2026-11-15 JaTin 增加个人历史房间访问记录与查询。
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "server/core/Database.h"
#include "server/core/Room.h"

namespace dp {

/**
 * @brief   一个账号进过的房间及其最近动态（历史房间列表条目）。
 * @author  JaTin
 */
struct VisitedRoom_S {
    /**
     * @brief  房间号
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string roomId;
    /**
     * @brief  该账号最后一次进入此房间的时间（毫秒时间戳）
     * @note   类型 int64_t；类/结构体成员变量。
     */
    int64_t lastVisitAt = 0;
    /**
     * @brief  房间内最后一条消息的发送者昵称；无消息时为空串
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string lastSender;
    /**
     * @brief  房间内最后一条消息的文本预览；语音消息为 "[语音]"；无消息时为空串
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string lastMessage;
    /**
     * @brief  房间内最后一条消息的时间戳；无消息时为 0
     * @note   类型 int64_t；类/结构体成员变量。
     */
    int64_t lastTs = 0;
};

/**
 * @brief   聊天记录表的访问对象。
 * @author  JaTin
 */
class CMessageRepository {
public:
    CMessageRepository() = default;
    CMessageRepository(const CMessageRepository&) = delete;
    CMessageRepository& operator=(const CMessageRepository&) = delete;

    /**
     * @brief      打开聊天记录数据库。
     * @param[in] path           文件路径（不存在则创建）；类型 const std::string&。
     * @param[in] error          失败原因；类型 std::string*。
     * @return     true 打开成功。
     * @author     JaTin
     */
    bool open(const std::string& path, std::string* error);

    /**
     * @brief      建表与索引（幂等）。
     * @param[in] error          失败原因；类型 std::string*。
     * @return     true 表结构已就绪。
     * @author     JaTin
     */
    bool ensureSchema(std::string* error);

    /**
     * @brief  是否已打开。
     */
    bool isOpen() const { return db_.isOpen(); }

    /**
     * @brief 写入一条消息。
     * @param roomId    房间号（`HistoryEntry_S` 本身不带房间字段，由调用方提供）。
     * @param entry     历史条目（文本或语音）。
     * @param accountId 持久用户 id，0 表示匿名（未匹配到用户库）。
     * @param error     失败原因。
     * @retval true 写入成功。
     * @details 以 `(room_id, seq)` 作为唯一键，重复写入会被忽略而不是报错——
     *          这样服务端重启后重放同一条消息不会产生重复记录。
     */
    bool append(const std::string& roomId, const HistoryEntry_S& entry, int64_t accountId,
                std::string* error);

    /**
     * @brief      读取某房间最近的若干条消息。
     * @param[in] roomId         房间号；类型 const std::string&。
     * @param[in] limit          最多返回多少条；类型 int。
     * @return     按 seq 正序排列；`from` 字段回填为发送者的连接 id（与内存缓冲一致）。
     * @author     JaTin
     */
    std::vector<HistoryEntry_S> loadRecent(const std::string& roomId, int limit);

    /**
     * @brief      记录一次「账号进入房间」。
     * @details    仅进房未发言也算历史房间；重复进入时刷新 last_visit_at。 匿名（accountId <= 0）不记录。
     * @param[in] accountId     持久账号编号；类型 int64_t。
     * @param[in] roomId        房间号；类型 const std::string&。
     * @return     记录成功返回 true；库未打开或执行失败返回 false。
     * @author     JaTin
     */
    bool recordVisit(int64_t accountId, const std::string& roomId);

    /**
     * @brief      查询一个账号进过的全部房间，按最近访问时间倒序。
     * @param[in] accountId     持久账号编号；类型 int64_t。
     * @param[in] limit         最多返回多少条；类型 int。
     * @return     房间列表；账号无效或无记录时为空。
     * @author     JaTin
     */
    std::vector<VisitedRoom_S> loadVisitedRooms(int64_t accountId, int limit);

    /**
     * @brief      记录总数。
     * @note       本函数无输入参数。
     * @return     返回计数或长度；无有效数据时返回 0。
     * @author     JaTin
     */
    size_t count();

    /**
     * @brief 把超过保留期的记录搬到归档库，再从主库删除。
     * @param retentionDays 保留天数，<=0 表示不清理。
     * @param archivePath   归档库文件路径（父目录需已存在）。
     * @param error         失败原因。
     * @retval 本次归档并删除的条数（0 表示没有到期数据或失败）。
     * @details 用 SQLite 的 `ATTACH` 在一条连接里完成「跨库复制 + 删除」，
     *          避免把数据先读进内存再写出去。归档库与主库表结构完全一致。
     *
     *          **先复制后删除**：中途失败最多留下重复记录（归档库有
     *          `INSERT OR IGNORE` + 唯一键保护），不会丢数据。
     */
    int archiveAndPrune(int retentionDays, const std::string& archivePath,
                        std::string* error);

    /**
     * @brief      统计超过保留期的记录数（不删除）。
     * @param[in] retentionDays  保留天数；类型 int。
     * @return     到期条数。
     * @author     JaTin
     */
    size_t countExpired(int retentionDays);

private:
    /**
     * @brief  messages.db 连接
     * @note   类型 CDatabase；类/结构体成员变量。
     */
    CDatabase db_;
};

/**
 * @brief  namespace dp。
 */
}
