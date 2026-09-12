// 回收站仓储（阶段八）。
//
// 文件和文件夹是两张表，但回收站必须能统一分页排序，所以这里用 UNION ALL
// 把两者拼成一个列表（排序字段走白名单映射到固定 SQL，不拼用户输入）。
//
// 永久删除的约定：
//   * 事务里先取出要删的对象 Key 与总字节数，再删数据库记录（文件夹级联删文件）
//   * **不在事务里删磁盘对象**；调用方把 Key 写进 cleanup_tasks 交给后台任务
//   * 同一事务里按总字节数回收用户配额
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lan_file_hub/common/pagination.h"
#include "lan_file_hub/common/result.h"
#include "lan_file_hub/infrastructure/database.h"

namespace lan_file_hub {

// 回收站里的一条记录（文件或文件夹）
struct TrashItem {
  std::string id;
  std::string kind;  // "file" | "folder"
  std::string name;
  std::uint64_t size_bytes{0};  // 文件夹为 0
  std::string folder_id;        // 原父目录（可能已被删除）
  std::string folder_name;      // 便于前端提示「原位置」
  std::string owner_id;
  std::string deleted_at;
};

struct TrashListing {
  std::vector<TrashItem> items;
  std::uint64_t total{0};
  int page{1};
  int page_size{kDefaultPageSize};
};

// 永久删除时需要清理的内容
struct PurgeResult {
  std::vector<std::string> storage_keys;  // 去重后的对象 Key
  std::uint64_t freed_bytes{0};           // 回收的配额字节数
  std::uint64_t file_count{0};
  std::uint64_t folder_count{0};
  // 文件夹删掉之后 path_key 就查不到了，调用方需要用它清理子树里的授权与分享链接
  std::string path_key_prefix;
};

class TrashRepository {
 public:
  explicit TrashRepository(ConnectionPool& pool) : pool_(pool) {}

  // owner_id 为空表示管理员视角（列出全部用户）
  [[nodiscard]] Result<TrashListing> List(std::string_view owner_id, const Pagination& pagination,
                                          const std::string& order_by,
                                          std::string_view kind_filter);

  [[nodiscard]] Result<std::optional<TrashItem>> FindItem(std::string_view owner_id,
                                                          std::string_view resource_id);

  // 该资源是否存在于该用户名下且当前是 ACTIVE（用来区分「已经恢复过」与「根本不属于你」）
  [[nodiscard]] Result<bool> IsActiveOwnedBy(std::string_view owner_id,
                                             std::string_view resource_id);

  // 恢复文件：把状态改回 ACTIVE。目标目录不存在时由调用方决定放到哪里。
  [[nodiscard]] Status RestoreFile(std::string_view file_id, std::string_view target_folder_id);

  // 恢复文件夹子树（含其中的文件），返回恢复的文件夹数
  [[nodiscard]] Result<std::uint64_t> RestoreFolderSubtree(std::string_view folder_id);

  // 该文件夹的父目录是否仍然可用（ACTIVE）
  [[nodiscard]] Result<bool> FolderAvailable(std::string_view folder_id);

  // 目录下（含子树）是否已有同名有效文件
  [[nodiscard]] Result<bool> ActiveNameExists(std::string_view folder_id, std::string_view name,
                                              std::string_view exclude_file_id);

  // 同一个父目录下是否存在同名活动文件夹
  [[nodiscard]] Result<bool> ActiveFolderNameExists(std::string_view parent_id,
                                                    std::string_view name,
                                                    std::string_view exclude_folder_id);

  // 永久删除一个文件（含其全部版本）：返回待清理对象
  [[nodiscard]] Result<PurgeResult> PurgeFile(std::string_view file_id);

  // 永久删除文件夹子树
  [[nodiscard]] Result<PurgeResult> PurgeFolder(std::string_view folder_id);

  // 过期的回收站条目（按 deleted_at 从早到晚）。文件夹优先，避免同一子树被处理两次。
  [[nodiscard]] Result<std::vector<TrashItem>> ListExpired(std::int64_t retention_seconds,
                                                           int limit);

  // 用户根目录 id（恢复到根目录时用）
  [[nodiscard]] Result<std::optional<std::string>> RootFolderId(std::string_view owner_id);

 private:
  ConnectionPool& pool_;
};

}  // namespace lan_file_hub
