// 文件夹仓储。所有 SQL 参数化；子树操作靠 path_key 前缀完成。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lan_file_hub/common/pagination.h"
#include "lan_file_hub/common/result.h"
#include "lan_file_hub/domain/folder.h"
#include "lan_file_hub/infrastructure/database.h"

namespace lan_file_hub {

class FolderRepository {
 public:
  explicit FolderRepository(ConnectionPool& pool) : pool_(pool) {}

  // 插入。同一父目录下重名时唯一索引冲突 → RESOURCE_CONFLICT。
  [[nodiscard]] Result<Folder> Create(const Folder& folder);

  [[nodiscard]] Result<std::optional<Folder>> FindById(std::string_view id);

  // 取用户根目录；不存在则创建（幂等）。注册后即便漏建也能自愈。
  [[nodiscard]] Result<Folder> EnsureRoot(std::string_view owner_id);

  // 普通列表：只返回 ACTIVE 的直接子目录，不含已删除的。
  [[nodiscard]] Result<std::vector<Folder>> ListChildren(std::string_view parent_id,
                                                         const Pagination& pagination,
                                                         const std::string& order_by);

  [[nodiscard]] Result<std::uint64_t> CountChildren(std::string_view parent_id);

  // 按名字搜索自己的有效文件夹（2026-10-03，搜索功能）。
  // query 是**原始关键字**（子串匹配），通配符转义在仓储内部完成。
  [[nodiscard]] Result<std::vector<Folder>> SearchByOwner(std::string_view owner_id,
                                                          std::string_view query, int limit);

  // 取祖先链（含自身），按从根到自身排序。
  // 实现：任何祖先的 path_key 都是自身 path_key 的前缀，一条 LIKE 就能取全。
  [[nodiscard]] Result<std::vector<Folder>> ListAncestors(std::string_view folder_path_key);

  [[nodiscard]] Status UpdateName(std::string_view id, std::string_view new_name);

  // 移动：改 parent_id 并重写整棵子树的 path_key。
  // new_prefix = 新父目录的 path_key + 自身 id + "/"
  [[nodiscard]] Status Move(std::string_view id, std::string_view new_parent_id,
                            std::string_view new_prefix);

  // 软删除整棵子树（含自己）。
  [[nodiscard]] Status SoftDeleteSubtree(std::string_view id);

  // 取整棵子树（含自己）的 path_key 前缀，供权限与后续阶段使用。
  [[nodiscard]] Result<std::string> GetPathKey(std::string_view id);

  // 仅供测试：清空 folders 表。
  [[nodiscard]] Status DeleteAllForTesting();

 private:
  ConnectionPool& pool_;
};

}  // namespace lan_file_hub
