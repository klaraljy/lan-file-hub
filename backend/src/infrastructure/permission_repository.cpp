#include "lan_file_hub/infrastructure/permission_repository.h"

#include <array>
#include <sstream>

#include <pqxx/pqxx>
#include <openssl/rand.h>

#include "lan_file_hub/common/sha256.h"
#include "lan_file_hub/common/uuid.h"

#include "infrastructure/database_error.h"

namespace lan_file_hub {
namespace {

constexpr char kGrantColumns[] =
    "p.id, p.resource_type, p.resource_id::text AS resource_id, p.subject_type, "
    "p.subject_id::text AS subject_id, COALESCE(u.username, '') AS subject_name, "
    "p.permission, "
    "to_char(p.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at, "
    "to_char(p.updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS updated_at";

constexpr char kShareColumns[] =
    "s.id, s.token_hash, s.resource_type, s.resource_id::text AS resource_id, s.permission, "
    "to_char(s.expires_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS expires_at, "
    "s.max_downloads, s.download_count, s.password_hash, "
    "to_char(s.revoked_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS revoked_at, "
    "s.created_by::text AS created_by, "
    "to_char(s.last_access_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') "
    "AS last_access_at, "
    "to_char(s.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at, "
    "COALESCE(f.original_name, d.name, '') AS resource_name";

// 分享链接要带上资源名，方便列表展示；资源被删除后名字为空
constexpr char kShareFrom[] =
    "FROM share_links s "
    "LEFT JOIN files f ON s.resource_type = 'file' AND f.id = s.resource_id "
    "LEFT JOIN folders d ON s.resource_type = 'folder' AND d.id = s.resource_id";

template <typename RowType>
PermissionGrant GrantFromRow(const RowType& row) {
  PermissionGrant grant;
  grant.id = row["id"].template as<std::string>();
  grant.resource_type = row["resource_type"].template as<std::string>();
  grant.resource_id = row["resource_id"].template as<std::string>();
  grant.subject_type = row["subject_type"].template as<std::string>();
  grant.subject_id = row["subject_id"].template as<std::string>();
  grant.subject_name = row["subject_name"].template as<std::string>();
  grant.permission = row["permission"].template as<std::string>();
  grant.created_at = row["created_at"].template as<std::string>();
  grant.updated_at = row["updated_at"].template as<std::string>();
  return grant;
}

template <typename RowType>
ShareLink ShareFromRow(const RowType& row) {
  ShareLink link;
  link.id = row["id"].template as<std::string>();
  link.token_hash = row["token_hash"].template as<std::string>();
  link.resource_type = row["resource_type"].template as<std::string>();
  link.resource_id = row["resource_id"].template as<std::string>();
  link.permission = row["permission"].template as<std::string>();
  if (!row["expires_at"].is_null()) {
    link.expires_at = row["expires_at"].template as<std::string>();
  }
  if (!row["max_downloads"].is_null()) {
    link.max_downloads = row["max_downloads"].template as<std::int32_t>();
  }
  link.download_count = row["download_count"].template as<std::int32_t>();
  link.password_hash = row["password_hash"].template as<std::string>();
  if (!row["revoked_at"].is_null()) {
    link.revoked_at = row["revoked_at"].template as<std::string>();
  }
  link.created_by = row["created_by"].template as<std::string>();
  if (!row["last_access_at"].is_null()) {
    link.last_access_at = row["last_access_at"].template as<std::string>();
  }
  link.created_at = row["created_at"].template as<std::string>();
  if (!row["resource_name"].is_null()) {
    link.resource_name = row["resource_name"].template as<std::string>();
  }
  return link;
}

}  // namespace

// ---- PermissionRepository ---------------------------------------------------

Result<PermissionGrant> PermissionRepository::Upsert(std::string_view resource_type,
                                                     std::string_view resource_id,
                                                     std::string_view subject_id,
                                                     std::string_view permission,
                                                     std::string_view created_by) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(GenerateUuidV4());
    params.append(std::string(resource_type));
    params.append(std::string(resource_id));
    params.append(std::string(subject_id));
    params.append(std::string(permission));
    if (created_by.empty()) {
      params.append(nullptr);
    } else {
      params.append(std::string(created_by));
    }
    // 一个用户对一个资源只有一条记录：重复授予就是改级别
    const pqxx::result rows = tx.exec(
        "INSERT INTO permissions (id, resource_type, resource_id, subject_type, subject_id, "
        "permission, created_by) "
        "VALUES ($1::uuid, $2, $3::uuid, 'USER', $4::uuid, $5, $6::uuid) "
        "ON CONFLICT (resource_type, resource_id, subject_type, subject_id) "
        "DO UPDATE SET permission = EXCLUDED.permission, updated_at = now() "
        "RETURNING id",
        params);
    tx.commit();
    if (rows.empty()) {
      return Error{"INTERNAL_ERROR", "授予权限未返回记录"};
    }
    const std::string grant_id = rows[0]["id"].as<std::string>();

    // 回读完整记录（带用户名）
    pqxx::work read_tx(lease->connection());
    pqxx::params read_params;
    read_params.append(grant_id);
    const pqxx::result full = read_tx.exec(
        "SELECT " + std::string(kGrantColumns) +
            " FROM permissions p LEFT JOIN users u ON u.id = p.subject_id WHERE p.id = $1::uuid",
        read_params);
    read_tx.commit();
    if (full.empty()) {
      return Error{"INTERNAL_ERROR", "读取授权记录失败"};
    }
    return GrantFromRow(full[0]);
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "授予权限失败");
  }
}

Status PermissionRepository::Remove(std::string_view resource_type, std::string_view resource_id,
                                    std::string_view subject_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(resource_type));
    params.append(std::string(resource_id));
    params.append(std::string(subject_id));
    // 删除不存在的授权也算成功（幂等）
    tx.exec(
        "DELETE FROM permissions WHERE resource_type = $1 AND resource_id = $2::uuid "
        "AND subject_id = $3::uuid",
        params);
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "删除权限失败"));
  }
}

Result<std::vector<PermissionGrant>> PermissionRepository::ListForResource(
    std::string_view resource_type, std::string_view resource_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(resource_type));
    params.append(std::string(resource_id));
    const pqxx::result rows = tx.exec(
        "SELECT " + std::string(kGrantColumns) +
            " FROM permissions p LEFT JOIN users u ON u.id = p.subject_id "
            "WHERE p.resource_type = $1 AND p.resource_id = $2::uuid "
            "ORDER BY CASE p.permission WHEN 'OWNER' THEN 0 WHEN 'WRITE' THEN 1 ELSE 2 END, "
            "u.username ASC",
        params);
    tx.commit();
    std::vector<PermissionGrant> grants;
    grants.reserve(rows.size());
    for (const auto& row : rows) {
      grants.push_back(GrantFromRow(row));
    }
    return grants;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询权限列表失败");
  }
}

Result<std::optional<std::string>> PermissionRepository::FindDirect(std::string_view resource_type,
                                                                    std::string_view resource_id,
                                                                    std::string_view subject_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(resource_type));
    params.append(std::string(resource_id));
    params.append(std::string(subject_id));
    const pqxx::result rows = tx.exec(
        "SELECT permission FROM permissions WHERE resource_type = $1 AND resource_id = $2::uuid "
        "AND subject_type = 'USER' AND subject_id = $3::uuid",
        params);
    tx.commit();
    if (rows.empty()) {
      return std::optional<std::string>{};
    }
    return std::optional<std::string>{rows[0]["permission"].as<std::string>()};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询权限失败");
  }
}

Result<std::vector<std::string>> PermissionRepository::ListLevelsOnPath(std::string_view subject_id,
                                                                        std::string_view path_key) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(subject_id));
    params.append(std::string(path_key));
    // path_key 形如 /a/b/c/：祖先是所有 path_key 为其前缀的文件夹。
    // 只统计 ACTIVE 的文件夹，避免用已删除目录上的旧授权放行。
    const pqxx::result rows = tx.exec(
        "SELECT p.permission FROM permissions p JOIN folders f ON f.id = p.resource_id "
        "WHERE p.resource_type = 'folder' AND p.subject_type = 'USER' AND p.subject_id = $1::uuid "
        "AND f.status = 'ACTIVE' AND $2 LIKE f.path_key || '%'",
        params);
    tx.commit();
    std::vector<std::string> levels;
    levels.reserve(rows.size());
    for (const auto& row : rows) {
      levels.push_back(row["permission"].as<std::string>());
    }
    return levels;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询继承权限失败");
  }
}

Status PermissionRepository::DeleteForResource(std::string_view resource_type,
                                               std::string_view resource_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(resource_type));
    params.append(std::string(resource_id));
    tx.exec("DELETE FROM permissions WHERE resource_type = $1 AND resource_id = $2::uuid", params);
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "清理授权记录失败"));
  }
}

Status PermissionRepository::DeleteForFolderSubtree(std::string_view path_key_prefix) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(path_key_prefix) + "%");
    // 子树里的文件夹授权一次性清掉，避免留下指向已删除目录的悬空授权
    tx.exec(
        "DELETE FROM permissions WHERE resource_type = 'folder' AND resource_id IN "
        "(SELECT id FROM folders WHERE path_key LIKE $1)",
        params);
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "清理子目录授权失败"));
  }
}

Result<std::uint64_t> PermissionRepository::CountForSubject(std::string_view subject_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(subject_id));
    const pqxx::result rows = tx.exec(
        "SELECT count(*)::bigint AS total FROM permissions WHERE subject_id = $1::uuid", params);
    tx.commit();
    if (rows.empty()) {
      return static_cast<std::uint64_t>(0);
    }
    return rows[0]["total"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计授权失败");
  }
}

Result<std::vector<PermissionRepository::SharedResource>> PermissionRepository::ListSharedWith(
    std::string_view subject_id, int limit) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(subject_id));
    params.append(limit);
    // 只列 ACTIVE 资源：已删除的资源出现在「分享给我的」里没有意义
    const pqxx::result rows = tx.exec(
        "SELECT p.resource_type, p.resource_id::text AS resource_id, p.permission, "
        "       COALESCE(f.original_name, d.name, '') AS name, "
        "       COALESCE(f.size_bytes, 0)::bigint AS size_bytes, "
        "       COALESCE(f.owner_id, d.owner_id)::text AS owner_id, "
        "       to_char(p.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') "
        "         AS granted_at "
        "FROM permissions p "
        "LEFT JOIN files f ON p.resource_type = 'file' AND f.id = p.resource_id "
        "                AND f.status = 'ACTIVE' "
        "LEFT JOIN folders d ON p.resource_type = 'folder' AND d.id = p.resource_id "
        "                AND d.status = 'ACTIVE' "
        "WHERE p.subject_type = 'USER' AND p.subject_id = $1::uuid "
        "  AND (f.id IS NOT NULL OR d.id IS NOT NULL) "
        "ORDER BY p.created_at DESC LIMIT $2",
        params);
    tx.commit();

    std::vector<SharedResource> items;
    items.reserve(rows.size());
    for (const auto& row : rows) {
      SharedResource item;
      item.resource_type = row["resource_type"].as<std::string>();
      item.resource_id = row["resource_id"].as<std::string>();
      item.name = row["name"].as<std::string>();
      item.size_bytes = row["size_bytes"].as<std::uint64_t>();
      item.owner_id = row["owner_id"].as<std::string>();
      item.permission = row["permission"].as<std::string>();
      item.granted_at = row["granted_at"].as<std::string>();
      items.push_back(std::move(item));
    }
    return items;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询分享给我的资源失败");
  }
}

Status PermissionRepository::DeleteAllForTesting() {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    tx.exec("DELETE FROM permissions");
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "清空 permissions 表失败"));
  }
}

// ---- ShareLinkRepository ----------------------------------------------------

Result<ShareLink> ShareLinkRepository::Create(const ShareLink& link) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(link.id);
    params.append(link.token_hash);
    params.append(link.resource_type);
    params.append(link.resource_id);
    params.append(link.permission);
    if (link.expires_in_seconds.has_value()) {
      params.append(*link.expires_in_seconds);
    } else {
      params.append(nullptr);
    }
    if (link.expires_at.has_value()) {
      params.append(*link.expires_at);
    } else {
      params.append(nullptr);
    }
    if (link.max_downloads.has_value()) {
      params.append(*link.max_downloads);
    } else {
      params.append(nullptr);
    }
    params.append(link.created_by);
    // 分享密码的哈希（空串 = 不需要密码）；明文不经过这里
    params.append(link.password_hash);
    // 相对有效期由数据库算（now() + interval），绝对时间直接用；都没有就是不过期
    const pqxx::result rows = tx.exec(
        "INSERT INTO share_links (id, token_hash, resource_type, resource_id, permission, "
        "expires_at, max_downloads, created_by, password_hash) "
        "VALUES ($1::uuid, $2, $3, $4::uuid, $5, "
        "        CASE WHEN $6::bigint IS NOT NULL THEN now() + make_interval(secs => $6::bigint) "
        "             ELSE $7::timestamptz END, "
        "        $8, $9::uuid, $10) "
        "RETURNING id",
        params);
    tx.commit();
    if (rows.empty()) {
      return Error{"INTERNAL_ERROR", "创建分享链接未返回记录"};
    }
    // 回读完整记录：过期时间是数据库按 now() 算出来的，必须从库里读回来
    pqxx::work read_tx(lease->connection());
    pqxx::params read_params;
    read_params.append(link.id);
    const pqxx::result full = read_tx.exec("SELECT " + std::string(kShareColumns) + " " +
                                               std::string(kShareFrom) + " WHERE s.id = $1::uuid",
                                           read_params);
    read_tx.commit();
    if (full.empty()) {
      return Error{"INTERNAL_ERROR", "创建分享链接后读取失败"};
    }
    ShareLink created = ShareFromRow(full[0]);
    return created;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "创建分享链接失败");
  }
}

Result<std::optional<ShareLink>> ShareLinkRepository::FindUsableByTokenHash(
    std::string_view token_hash) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(token_hash));
    // 过期与撤销的记录在这里就被过滤掉，**不允许降级放行**
    const pqxx::result rows =
        tx.exec("SELECT " + std::string(kShareColumns) + " " + std::string(kShareFrom) +
                    " WHERE s.token_hash = $1 AND s.revoked_at IS NULL "
                    "AND (s.expires_at IS NULL OR s.expires_at > now()) "
                    "AND (s.max_downloads IS NULL OR s.download_count < s.max_downloads)",
                params);
    tx.commit();
    if (rows.empty()) {
      return std::optional<ShareLink>{};
    }
    return std::optional<ShareLink>{ShareFromRow(rows[0])};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询分享链接失败");
  }
}

Result<std::vector<ShareLink>> ShareLinkRepository::ListForResource(std::string_view resource_type,
                                                                    std::string_view resource_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(resource_type));
    params.append(std::string(resource_id));
    const pqxx::result rows =
        tx.exec("SELECT " + std::string(kShareColumns) + " " + std::string(kShareFrom) +
                    " WHERE s.resource_type = $1 AND s.resource_id = $2::uuid "
                    "ORDER BY s.created_at DESC",
                params);
    tx.commit();
    std::vector<ShareLink> links;
    links.reserve(rows.size());
    for (const auto& row : rows) {
      links.push_back(ShareFromRow(row));
    }
    return links;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询分享链接列表失败");
  }
}

Result<std::optional<ShareLink>> ShareLinkRepository::FindById(std::string_view id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(id));
    const pqxx::result rows = tx.exec("SELECT " + std::string(kShareColumns) + " " +
                                          std::string(kShareFrom) + " WHERE s.id = $1::uuid",
                                      params);
    tx.commit();
    if (rows.empty()) {
      return std::optional<ShareLink>{};
    }
    return std::optional<ShareLink>{ShareFromRow(rows[0])};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询分享链接失败");
  }
}

Result<bool> ShareLinkRepository::Revoke(std::string_view id, std::string_view actor_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(id));
    params.append(std::string(actor_id));
    // 只改 created_by 是自己的记录：别人的分享链接撤销不了（同时保证幂等）
    const pqxx::result rows = tx.exec(
        "UPDATE share_links SET revoked_at = now(), updated_at = now() "
        "WHERE id = $1::uuid AND created_by = $2::uuid AND revoked_at IS NULL RETURNING id",
        params);
    tx.commit();
    return !rows.empty();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "撤销分享链接失败");
  }
}

Result<bool> ShareLinkRepository::ConsumeDownload(std::string_view id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(id));
    // 一条 UPDATE 同时完成「检查是否还有次数」与「计数 +1」，
    // 并发下载不会同时通过检查（阶段九的下载次数要求）
    const pqxx::result rows = tx.exec(
        "UPDATE share_links SET download_count = download_count + 1, "
        "last_access_at = now(), updated_at = now() "
        "WHERE id = $1::uuid AND revoked_at IS NULL "
        "AND (expires_at IS NULL OR expires_at > now()) "
        "AND (max_downloads IS NULL OR download_count < max_downloads) "
        "RETURNING id",
        params);
    tx.commit();
    return !rows.empty();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "更新下载次数失败");
  }
}

Result<std::uint64_t> ShareLinkRepository::CountByResource(std::string_view resource_type,
                                                           std::string_view resource_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(resource_type));
    params.append(std::string(resource_id));
    const pqxx::result rows = tx.exec(
        "SELECT count(*)::bigint AS total FROM share_links "
        "WHERE resource_type = $1 AND resource_id = $2::uuid",
        params);
    tx.commit();
    if (rows.empty()) {
      return static_cast<std::uint64_t>(0);
    }
    return rows[0]["total"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计分享链接失败");
  }
}

Status ShareLinkRepository::DeleteForResource(std::string_view resource_type,
                                              std::string_view resource_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(resource_type));
    params.append(std::string(resource_id));
    tx.exec("DELETE FROM share_links WHERE resource_type = $1 AND resource_id = $2::uuid", params);
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "清理分享链接失败"));
  }
}

Status ShareLinkRepository::DeleteForFolderSubtree(std::string_view path_key_prefix) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(path_key_prefix) + "%");
    tx.exec(
        "DELETE FROM share_links WHERE resource_type = 'folder' AND resource_id IN "
        "(SELECT id FROM folders WHERE path_key LIKE $1)",
        params);
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "清理子目录分享链接失败"));
  }
}

Status ShareLinkRepository::DeleteAllForTesting() {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    tx.exec("DELETE FROM share_links");
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "清空 share_links 表失败"));
  }
}

// ---- Token ------------------------------------------------------------------

Result<std::string> GenerateShareToken() {
  // 32 字节 = 256 位随机；base64url 后 43 个字符，足够抗暴力枚举
  std::array<unsigned char, 32> buffer{};
  if (RAND_bytes(buffer.data(), static_cast<int>(buffer.size())) != 1) {
    return Error{"INTERNAL_ERROR", "无法生成安全随机 Token"};
  }
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string token;
  token.reserve(43);
  std::uint32_t accumulator = 0;
  int bits = 0;
  for (const unsigned char byte : buffer) {
    accumulator = (accumulator << 8) | byte;
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      token.push_back(kAlphabet[(accumulator >> bits) & 0x3F]);
    }
  }
  if (bits > 0) {
    token.push_back(kAlphabet[(accumulator << (6 - bits)) & 0x3F]);
  }
  return token;
}

std::string HashShareToken(std::string_view token) {
  return Sha256Hex(token);
}

}  // namespace lan_file_hub
