#include "lan_file_hub/infrastructure/trash_repository.h"

#include <algorithm>
#include <set>

#include <pqxx/pqxx>

#include "infrastructure/database_error.h"

namespace lan_file_hub {
namespace {

// UNION ALL 的两半必须列名与类型完全一致；文件夹的 size_bytes 固定为 0。
// 排序在 SQL 外围做，order_by 由调用方通过白名单映射提供（不是用户输入）。
constexpr char kTrashSelect[] =
    "SELECT f.id::text AS id, 'file' AS kind, f.original_name AS name, f.size_bytes AS size_bytes, "
    "       f.folder_id::text AS folder_id, COALESCE(p.name, '') AS folder_name, "
    "       f.deleted_at AS deleted_at, f.owner_id::text AS owner_id "
    "FROM files f LEFT JOIN folders p ON p.id = f.folder_id "
    "WHERE f.status = 'TRASHED' "
    "UNION ALL "
    "SELECT d.id::text AS id, 'folder' AS kind, d.name AS name, 0::bigint AS size_bytes, "
    "       COALESCE(d.parent_id::text, '') AS folder_id, COALESCE(p.name, '') AS folder_name, "
    "       d.deleted_at AS deleted_at, d.owner_id::text AS owner_id "
    "FROM folders d LEFT JOIN folders p ON p.id = d.parent_id "
    "WHERE d.status = 'TRASHED' AND d.parent_id IS NOT NULL";

template <typename RowType>
TrashItem ItemFromRow(const RowType& row) {
  TrashItem item;
  item.id = row["id"].template as<std::string>();
  item.kind = row["kind"].template as<std::string>();
  item.name = row["name"].template as<std::string>();
  item.size_bytes = row["size_bytes"].template as<std::uint64_t>();
  item.folder_id = row["folder_id"].template as<std::string>();
  item.folder_name = row["folder_name"].template as<std::string>();
  item.owner_id = row["owner_id"].template as<std::string>();
  item.deleted_at = row["deleted_at"].template as<std::string>();
  return item;
}

}  // namespace

Result<TrashListing> TrashRepository::List(std::string_view owner_id, const Pagination& pagination,
                                           const std::string& order_by,
                                           std::string_view kind_filter) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());

    // 条件里的占位符编号由计数器生成，值一律绑定 —— 不把任何输入拼进 SQL。
    // 注意 owner_id 在子查询里已经 ::text，这里按文本比较，避免 text = uuid 的类型错误。
    std::string where;
    pqxx::params count_params;
    int next = 1;
    if (!owner_id.empty()) {
      where += " AND owner_id = $" + std::to_string(next++);
      count_params.append(std::string(owner_id));
    }
    if (kind_filter == "file" || kind_filter == "folder") {
      where += " AND kind = $" + std::to_string(next++);
      count_params.append(std::string(kind_filter));
    }

    TrashListing listing;
    listing.page = pagination.page;
    listing.page_size = pagination.page_size;

    {
      const pqxx::result rows = tx.exec("SELECT count(*)::bigint AS total FROM (" +
                                            std::string(kTrashSelect) + ") t WHERE 1=1" + where,
                                        count_params);
      listing.total = rows.empty() ? 0 : rows[0]["total"].as<std::uint64_t>();
    }

    {
      pqxx::params params;
      if (!owner_id.empty()) {
        params.append(std::string(owner_id));
      }
      if (kind_filter == "file" || kind_filter == "folder") {
        params.append(std::string(kind_filter));
      }
      const std::string limit_placeholder = "$" + std::to_string(next++);
      const std::string offset_placeholder = "$" + std::to_string(next++);
      params.append(pagination.page_size);
      params.append(pagination.Offset());
      const pqxx::result rows = tx.exec(
          "SELECT * FROM (" + std::string(kTrashSelect) + ") t WHERE 1=1" + where + " ORDER BY " +
              order_by + " LIMIT " + limit_placeholder + " OFFSET " + offset_placeholder,
          params);
      for (const auto& row : rows) {
        listing.items.push_back(ItemFromRow(row));
      }
    }

    tx.commit();
    return listing;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询回收站失败");
  }
}

Result<std::optional<TrashItem>> TrashRepository::FindItem(std::string_view owner_id,
                                                           std::string_view resource_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(resource_id));
    // 子查询里 id 已经 ::text，按文本比较（写成 $1::uuid 会报 text = uuid）
    std::string where = " WHERE id = $1";
    if (!owner_id.empty()) {
      params.append(std::string(owner_id));
      where += " AND owner_id = $2";
    }
    const pqxx::result rows =
        tx.exec("SELECT * FROM (" + std::string(kTrashSelect) + ") t" + where, params);
    tx.commit();
    if (rows.empty()) {
      return std::optional<TrashItem>{};
    }
    return std::optional<TrashItem>{ItemFromRow(rows[0])};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询回收站条目失败");
  }
}

Result<bool> TrashRepository::IsActiveOwnedBy(std::string_view owner_id,
                                              std::string_view resource_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(resource_id));
    std::string owner_clause;
    if (!owner_id.empty()) {
      params.append(std::string(owner_id));
      owner_clause = " AND owner_id = $2::uuid";
    }
    const pqxx::result rows = tx.exec(
        "SELECT 1 AS ok FROM files WHERE id = $1::uuid AND status = 'ACTIVE'" + owner_clause +
            " UNION ALL "
            "SELECT 1 AS ok FROM folders WHERE id = $1::uuid AND status = 'ACTIVE'" +
            owner_clause + " LIMIT 1",
        params);
    tx.commit();
    return !rows.empty();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "检查资源状态失败");
  }
}

Status TrashRepository::RestoreFile(std::string_view file_id, std::string_view target_folder_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(target_folder_id));
    params.append(std::string(file_id));
    const pqxx::result rows = tx.exec(
        "UPDATE files SET status = 'ACTIVE', deleted_at = NULL, folder_id = $1::uuid, "
        "updated_at = now() "
        "WHERE id = $2::uuid AND status = 'TRASHED' RETURNING id",
        params);
    if (!rows.empty()) {
      tx.commit();
      return Status::Success();
    }

    // 没更新到：要么记录不存在，要么已经恢复过。后者按幂等成功处理。
    pqxx::params check_params;
    check_params.append(std::string(file_id));
    const pqxx::result existing =
        tx.exec("SELECT status FROM files WHERE id = $1::uuid", check_params);
    tx.commit();
    if (existing.empty()) {
      return Status(Error{"RESOURCE_NOT_FOUND", "文件不存在"});
    }
    // 已经不在回收站（ACTIVE/PURGED）都算「无需再恢复」
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "恢复文件失败"));
  }
}

Result<std::uint64_t> TrashRepository::RestoreFolderSubtree(std::string_view folder_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());

    pqxx::params lock_params;
    lock_params.append(std::string(folder_id));
    const pqxx::result current =
        tx.exec("SELECT path_key FROM folders WHERE id = $1::uuid FOR UPDATE", lock_params);
    if (current.empty()) {
      return Error{"RESOURCE_NOT_FOUND", "文件夹不存在"};
    }
    const std::string prefix = current[0]["path_key"].as<std::string>();

    std::uint64_t restored = 0;
    {
      pqxx::params params;
      params.append(prefix + "%");
      tx.exec(
          "UPDATE folders SET status = 'ACTIVE', deleted_at = NULL, updated_at = now() "
          "WHERE path_key LIKE $1 AND status = 'TRASHED'",
          params);
      const pqxx::result folders = tx.exec(
          "SELECT count(*)::bigint AS total FROM folders WHERE path_key LIKE $1 AND "
          "status = 'ACTIVE'",
          params);
      restored = folders.empty() ? 0 : folders[0]["total"].as<std::uint64_t>();
    }
    // 子树里的文件一并恢复
    {
      pqxx::params params;
      params.append(prefix + "%");
      tx.exec(
          "UPDATE files SET status = 'ACTIVE', deleted_at = NULL, updated_at = now() "
          "WHERE status = 'TRASHED' AND folder_id IN "
          "(SELECT id FROM folders WHERE path_key LIKE $1)",
          params);
    }
    tx.commit();
    return restored;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "恢复文件夹失败");
  }
}

Result<bool> TrashRepository::FolderAvailable(std::string_view folder_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(folder_id));
    const pqxx::result rows =
        tx.exec("SELECT 1 AS ok FROM folders WHERE id = $1::uuid AND status = 'ACTIVE'", params);
    tx.commit();
    return !rows.empty();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "检查目录状态失败");
  }
}

Result<bool> TrashRepository::ActiveNameExists(std::string_view folder_id, std::string_view name,
                                               std::string_view exclude_file_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(folder_id));
    params.append(std::string(name));
    params.append(std::string(exclude_file_id));
    const pqxx::result rows = tx.exec(
        "SELECT 1 AS ok FROM files WHERE folder_id = $1::uuid AND status = 'ACTIVE' "
        "AND lower(original_name) = lower($2) AND id <> $3::uuid LIMIT 1",
        params);
    tx.commit();
    return !rows.empty();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "检查同名文件失败");
  }
}

Result<bool> TrashRepository::ActiveFolderNameExists(std::string_view parent_id,
                                                     std::string_view name,
                                                     std::string_view exclude_folder_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(parent_id));
    params.append(std::string(name));
    params.append(std::string(exclude_folder_id));
    const pqxx::result rows = tx.exec(
        "SELECT 1 AS ok FROM folders WHERE parent_id = $1::uuid AND status = 'ACTIVE' "
        "AND lower(name) = lower($2) AND id <> $3::uuid LIMIT 1",
        params);
    tx.commit();
    return !rows.empty();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "检查同名文件夹失败");
  }
}

Result<PurgeResult> TrashRepository::PurgeFile(std::string_view file_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());

    std::string owner_id;
    {
      pqxx::params params;
      params.append(std::string(file_id));
      const pqxx::result rows = tx.exec(
          "SELECT owner_id::text AS owner_id FROM files WHERE id = $1::uuid FOR UPDATE", params);
      if (rows.empty()) {
        return Error{"RESOURCE_NOT_FOUND", "文件不存在"};
      }
      owner_id = rows[0]["owner_id"].as<std::string>();
    }

    PurgeResult result;
    {
      pqxx::params params;
      params.append(std::string(file_id));
      // 同一内容可能被多个版本引用：去重后再交给清理任务，避免重复排队
      const pqxx::result rows = tx.exec(
          "SELECT DISTINCT storage_key FROM file_versions WHERE file_id = $1::uuid", params);
      for (const auto& row : rows) {
        result.storage_keys.push_back(row["storage_key"].as<std::string>());
      }
    }
    {
      pqxx::params params;
      params.append(std::string(file_id));
      const pqxx::result rows = tx.exec(
          "SELECT COALESCE(SUM(size_bytes), 0)::bigint AS total FROM file_versions "
          "WHERE file_id = $1::uuid",
          params);
      result.freed_bytes = rows.empty() ? 0 : rows[0]["total"].as<std::uint64_t>();
    }

    {
      pqxx::params params;
      params.append(std::string(file_id));
      tx.exec("DELETE FROM files WHERE id = $1::uuid", params);
    }
    {
      pqxx::params params;
      params.append(result.freed_bytes);
      params.append(owner_id);
      tx.exec(
          "UPDATE users SET used_storage_bytes = GREATEST(used_storage_bytes - $1, 0), "
          "updated_at = now() WHERE id = $2::uuid",
          params);
    }

    tx.commit();
    result.file_count = 1;
    return result;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "永久删除文件失败");
  }
}

Result<PurgeResult> TrashRepository::PurgeFolder(std::string_view folder_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());

    std::string owner_id;
    std::string prefix;
    {
      pqxx::params params;
      params.append(std::string(folder_id));
      const pqxx::result rows = tx.exec(
          "SELECT owner_id::text AS owner_id, path_key FROM folders WHERE id = $1::uuid "
          "FOR UPDATE",
          params);
      if (rows.empty()) {
        return Error{"RESOURCE_NOT_FOUND", "文件夹不存在"};
      }
      owner_id = rows[0]["owner_id"].as<std::string>();
      prefix = rows[0]["path_key"].as<std::string>();
    }

    PurgeResult result;
    {
      pqxx::params params;
      params.append(prefix + "%");
      const pqxx::result rows = tx.exec(
          "SELECT count(*)::bigint AS total FROM files WHERE folder_id IN "
          "(SELECT id FROM folders WHERE path_key LIKE $1)",
          params);
      result.file_count = rows.empty() ? 0 : rows[0]["total"].as<std::uint64_t>();
    }
    {
      pqxx::params params;
      params.append(prefix + "%");
      const pqxx::result rows =
          tx.exec("SELECT count(*)::bigint AS total FROM folders WHERE path_key LIKE $1", params);
      result.folder_count = rows.empty() ? 0 : rows[0]["total"].as<std::uint64_t>();
    }
    {
      pqxx::params params;
      params.append(prefix + "%");
      const pqxx::result rows = tx.exec(
          "SELECT DISTINCT v.storage_key FROM file_versions v JOIN files f ON f.id = v.file_id "
          "WHERE f.folder_id IN (SELECT id FROM folders WHERE path_key LIKE $1)",
          params);
      for (const auto& row : rows) {
        result.storage_keys.push_back(row["storage_key"].as<std::string>());
      }
    }
    {
      pqxx::params params;
      params.append(prefix + "%");
      const pqxx::result rows = tx.exec(
          "SELECT COALESCE(SUM(v.size_bytes), 0)::bigint AS total FROM file_versions v "
          "JOIN files f ON f.id = v.file_id "
          "WHERE f.folder_id IN (SELECT id FROM folders WHERE path_key LIKE $1)",
          params);
      result.freed_bytes = rows.empty() ? 0 : rows[0]["total"].as<std::uint64_t>();
    }

    {
      // 删文件夹会级联删掉子树文件夹与其中的文件（外键 ON DELETE CASCADE）
      pqxx::params params;
      params.append(std::string(folder_id));
      tx.exec("DELETE FROM folders WHERE id = $1::uuid", params);
    }
    {
      pqxx::params params;
      params.append(result.freed_bytes);
      params.append(owner_id);
      tx.exec(
          "UPDATE users SET used_storage_bytes = GREATEST(used_storage_bytes - $1, 0), "
          "updated_at = now() WHERE id = $2::uuid",
          params);
    }

    result.path_key_prefix = prefix;
    tx.commit();
    return result;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "永久删除文件夹失败");
  }
}

Result<std::vector<TrashItem>> TrashRepository::ListExpired(std::int64_t retention_seconds,
                                                            int limit) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(retention_seconds);
    params.append(limit);
    // 文件夹优先：一个文件夹的子树被删掉后，里面的文件自然消失，不会重复处理
    const pqxx::result rows =
        tx.exec("SELECT * FROM (" + std::string(kTrashSelect) +
                    ") t "
                    "WHERE deleted_at < now() - make_interval(secs => $1) "
                    "ORDER BY CASE WHEN kind = 'folder' THEN 0 ELSE 1 END, deleted_at ASC LIMIT $2",
                params);
    tx.commit();
    std::vector<TrashItem> items;
    items.reserve(rows.size());
    for (const auto& row : rows) {
      items.push_back(ItemFromRow(row));
    }
    return items;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询过期回收站条目失败");
  }
}

Result<std::optional<std::string>> TrashRepository::RootFolderId(std::string_view owner_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(owner_id));
    const pqxx::result rows = tx.exec(
        "SELECT id::text AS id FROM folders WHERE owner_id = $1::uuid AND parent_id IS NULL "
        "AND status = 'ACTIVE' LIMIT 1",
        params);
    tx.commit();
    if (rows.empty()) {
      return std::optional<std::string>{};
    }
    return std::optional<std::string>{rows[0]["id"].as<std::string>()};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询用户根目录失败");
  }
}

}  // namespace lan_file_hub
