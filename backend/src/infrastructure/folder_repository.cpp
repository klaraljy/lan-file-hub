#include "lan_file_hub/infrastructure/folder_repository.h"

#include <pqxx/pqxx>

#include "lan_file_hub/common/sql_like.h"
#include "lan_file_hub/common/uuid.h"

#include "infrastructure/database_error.h"

namespace lan_file_hub {
namespace {

constexpr char kSelectColumns[] =
    "id, owner_id, parent_id, name, path_key, status, "
    "to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at, "
    "to_char(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS updated_at, "
    "to_char(deleted_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS deleted_at";

template <typename RowType>
Folder FolderFromRow(const RowType& row) {
  Folder folder;
  folder.id = row["id"].template as<std::string>();
  folder.owner_id = row["owner_id"].template as<std::string>();
  if (!row["parent_id"].is_null()) {
    folder.parent_id = row["parent_id"].template as<std::string>();
  }
  folder.name = row["name"].template as<std::string>();
  folder.path_key = row["path_key"].template as<std::string>();
  folder.status = ParseFolderStatus(row["status"].template as<std::string>());
  folder.created_at = row["created_at"].template as<std::string>();
  folder.updated_at = row["updated_at"].template as<std::string>();
  if (!row["deleted_at"].is_null()) {
    folder.deleted_at = row["deleted_at"].template as<std::string>();
  }
  return folder;
}

}  // namespace

Result<Folder> FolderRepository::Create(const Folder& folder) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(folder.id);
    params.append(folder.owner_id);
    if (folder.parent_id.has_value()) {
      params.append(*folder.parent_id);
    } else {
      params.append(nullptr);
    }
    params.append(folder.name);
    params.append(folder.path_key);
    params.append(ToDbString(folder.status));

    const pqxx::result rows = tx.exec(
        "INSERT INTO folders (id, owner_id, parent_id, name, path_key, status) "
        "VALUES ($1::uuid, $2::uuid, $3::uuid, $4, $5, $6) RETURNING " +
            std::string(kSelectColumns),
        params);
    tx.commit();

    if (rows.empty()) {
      return Error{"INTERNAL_ERROR", "创建文件夹未返回记录"};
    }
    return FolderFromRow(rows[0]);
  } catch (const pqxx::unique_violation&) {
    return Error{"RESOURCE_CONFLICT", "同一目录下已存在同名文件夹"};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "创建文件夹失败");
  }
}

Result<std::optional<Folder>> FolderRepository::FindById(std::string_view id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(id));
    const pqxx::result rows = tx.exec(
        "SELECT " + std::string(kSelectColumns) + " FROM folders WHERE id = $1::uuid", params);
    tx.commit();

    if (rows.empty()) {
      return std::optional<Folder>{};
    }
    return std::optional<Folder>{FolderFromRow(rows[0])};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询文件夹失败");
  }
}

Result<Folder> FolderRepository::EnsureRoot(std::string_view owner_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());

    {
      pqxx::params params;
      params.append(std::string(owner_id));
      const pqxx::result rows =
          tx.exec("SELECT " + std::string(kSelectColumns) +
                      " FROM folders WHERE owner_id = $1::uuid AND parent_id IS NULL",
                  params);
      if (!rows.empty()) {
        tx.commit();
        return FolderFromRow(rows[0]);
      }
    }

    // 根目录的 path_key 必须包含自己的 id，所以先生成 id 再拼路径
    const std::string folder_id = GenerateUuidV4();
    const std::string path_key = "/" + folder_id + "/";

    pqxx::params params;
    params.append(folder_id);
    params.append(std::string(owner_id));
    params.append(path_key);
    const pqxx::result inserted = tx.exec(
        "INSERT INTO folders (id, owner_id, parent_id, name, path_key, status) "
        "VALUES ($1::uuid, $2::uuid, NULL, '', $3, 'ACTIVE') RETURNING " +
            std::string(kSelectColumns),
        params);
    tx.commit();

    if (inserted.empty()) {
      return Error{"INTERNAL_ERROR", "创建根目录未返回记录"};
    }
    return FolderFromRow(inserted[0]);
  } catch (const pqxx::unique_violation&) {
    // 并发注册/首次访问时可能撞上唯一索引，重新读一次即可
    return EnsureRoot(owner_id);
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "创建根目录失败");
  }
}

Result<std::vector<Folder>> FolderRepository::ListChildren(std::string_view parent_id,
                                                           const Pagination& pagination,
                                                           const std::string& order_by) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(parent_id));
    params.append(pagination.page_size);
    params.append(pagination.Offset());

    // order_by 来自白名单常量（见 FolderService），不是用户输入。
    // 表必须起别名 f：白名单里的排序片段用的是 f.xxx。
    const pqxx::result rows =
        tx.exec("SELECT " + std::string(kSelectColumns) +
                    " FROM folders f WHERE f.parent_id = $1::uuid AND f.status = 'ACTIVE' "
                    "ORDER BY " +
                    order_by + " LIMIT $2 OFFSET $3",
                params);
    tx.commit();

    std::vector<Folder> folders;
    folders.reserve(rows.size());
    for (const auto& row : rows) {
      folders.push_back(FolderFromRow(row));
    }
    return folders;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询子目录失败");
  }
}

// 按名字搜索自己的有效文件夹（2026-10-03，搜索功能）。
// 只搜自己的、只搜 ACTIVE（回收站里的不出现）；query 是**原始关键字**，
// 通配符由这里用公共助手转义（搜 `%` 不会变成"匹配全部"）。
Result<std::vector<Folder>> FolderRepository::SearchByOwner(std::string_view owner_id,
                                                            std::string_view query, int limit) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(owner_id));
    params.append(LikeContains(query));
    params.append(limit);
    const pqxx::result rows =
        tx.exec("SELECT " + std::string(kSelectColumns) +
                    " FROM folders f WHERE f.owner_id = $1::uuid AND f.status = 'ACTIVE' "
                    "AND f.name ILIKE $2 ESCAPE '\\' "
                    "ORDER BY f.updated_at DESC, f.id LIMIT $3",
                params);
    tx.commit();

    std::vector<Folder> folders;
    folders.reserve(rows.size());
    for (const auto& row : rows) {
      folders.push_back(FolderFromRow(row));
    }
    return folders;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "搜索文件夹失败");
  }
}

Result<std::uint64_t> FolderRepository::CountChildren(std::string_view parent_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(parent_id));
    const pqxx::result rows = tx.exec(
        "SELECT count(*)::bigint AS total FROM folders "
        "WHERE parent_id = $1::uuid AND status = 'ACTIVE'",
        params);
    tx.commit();
    if (rows.empty())
      return static_cast<std::uint64_t>(0);
    return rows[0]["total"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计子目录失败");
  }
}

Result<std::vector<Folder>> FolderRepository::ListAncestors(std::string_view folder_path_key) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(folder_path_key));
    // 祖先的 path_key 一定是自身 path_key 的前缀；按长度升序即从根到自身
    const pqxx::result rows =
        tx.exec("SELECT " + std::string(kSelectColumns) +
                    " FROM folders WHERE $1 LIKE path_key || '%' AND status = 'ACTIVE' "
                    "ORDER BY length(path_key) ASC",
                params);
    tx.commit();

    std::vector<Folder> chain;
    chain.reserve(rows.size());
    for (const auto& row : rows) {
      chain.push_back(FolderFromRow(row));
    }
    return chain;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询文件夹路径失败");
  }
}

Status FolderRepository::UpdateName(std::string_view id, std::string_view new_name) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(new_name));
    params.append(std::string(id));
    const pqxx::result rows = tx.exec(
        "UPDATE folders SET name = $1, updated_at = now() "
        "WHERE id = $2::uuid AND status = 'ACTIVE' AND parent_id IS NOT NULL RETURNING id",
        params);
    tx.commit();
    if (rows.empty()) {
      return Status(Error{"RESOURCE_NOT_FOUND", "文件夹不存在或不可重命名"});
    }
    return Status::Success();
  } catch (const pqxx::unique_violation&) {
    return Status(Error{"RESOURCE_CONFLICT", "同一目录下已存在同名文件夹"});
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "重命名文件夹失败"));
  }
}

Status FolderRepository::Move(std::string_view id, std::string_view new_parent_id,
                              std::string_view new_prefix) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }

  try {
    pqxx::work tx(lease->connection());

    // 锁住这一行，避免并发移动把 path_key 撕成两半
    pqxx::params lock_params;
    lock_params.append(std::string(id));
    const pqxx::result current =
        tx.exec("SELECT path_key FROM folders WHERE id = $1::uuid AND status = 'ACTIVE' FOR UPDATE",
                lock_params);
    if (current.empty()) {
      tx.commit();
      return Status(Error{"RESOURCE_NOT_FOUND", "文件夹不存在或已删除"});
    }
    const std::string old_prefix = current[0]["path_key"].as<std::string>();

    // 1) 先重写后代：它们仍以旧前缀开头
    {
      pqxx::params params;
      params.append(std::string(new_prefix));
      params.append(std::string(old_prefix) + "%");
      params.append(static_cast<int>(old_prefix.size()));
      params.append(std::string(id));
      tx.exec(
          "UPDATE folders SET path_key = $1 || substring(path_key from $3 + 1), updated_at = now() "
          "WHERE path_key LIKE $2 AND id <> $4::uuid",
          params);
    }

    // 2) 再改自己
    {
      pqxx::params params;
      params.append(std::string(new_parent_id));
      params.append(std::string(new_prefix));
      params.append(std::string(id));
      tx.exec(
          "UPDATE folders SET parent_id = $1::uuid, path_key = $2, updated_at = now() "
          "WHERE id = $3::uuid",
          params);
    }

    tx.commit();
    return Status::Success();
  } catch (const pqxx::unique_violation&) {
    return Status(Error{"RESOURCE_CONFLICT", "目标目录下已存在同名文件夹"});
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "移动文件夹失败"));
  }
}

Status FolderRepository::SoftDeleteSubtree(std::string_view id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }

  try {
    pqxx::work tx(lease->connection());

    pqxx::params lock_params;
    lock_params.append(std::string(id));
    const pqxx::result current =
        tx.exec("SELECT path_key FROM folders WHERE id = $1::uuid AND status = 'ACTIVE' FOR UPDATE",
                lock_params);
    if (current.empty()) {
      tx.commit();
      return Status(Error{"RESOURCE_NOT_FOUND", "文件夹不存在或已删除"});
    }
    const std::string prefix = current[0]["path_key"].as<std::string>();

    pqxx::params params;
    params.append(prefix + "%");
    tx.exec(
        "UPDATE folders SET status = 'TRASHED', deleted_at = now(), updated_at = now() "
        "WHERE path_key LIKE $1 AND status = 'ACTIVE'",
        params);
    // 子树里的文件一起进回收站：这样回收站是一份完整清单，
    // 用户能单独看到并恢复其中某个文件（而不是只能整棵子树一起恢复）
    tx.exec(
        "UPDATE files SET status = 'TRASHED', deleted_at = now(), updated_at = now() "
        "WHERE status = 'ACTIVE' AND folder_id IN "
        "(SELECT id FROM folders WHERE path_key LIKE $1)",
        params);
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "删除文件夹失败"));
  }
}

Result<std::string> FolderRepository::GetPathKey(std::string_view id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(id));
    const pqxx::result rows = tx.exec("SELECT path_key FROM folders WHERE id = $1::uuid", params);
    tx.commit();
    if (rows.empty()) {
      return Error{"RESOURCE_NOT_FOUND", "文件夹不存在"};
    }
    return rows[0]["path_key"].as<std::string>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询文件夹路径失败");
  }
}

Status FolderRepository::DeleteAllForTesting() {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    tx.exec("DELETE FROM folders");
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "清空 folders 表失败"));
  }
}

}  // namespace lan_file_hub
