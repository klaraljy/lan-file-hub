#include "lan_file_hub/infrastructure/file_repository.h"

#include <pqxx/pqxx>

#include "infrastructure/database_error.h"

namespace lan_file_hub {
namespace {

constexpr char kFileColumns[] =
    "id, owner_id, folder_id, current_version_id, original_name, mime_type, size_bytes, "
    "content_hash, storage_key, status, "
    "to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at, "
    "to_char(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS updated_at, "
    "to_char(deleted_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS deleted_at";

constexpr char kVersionColumns[] =
    "id, file_id, version_number, content_hash, size_bytes, storage_key, created_by, is_current, "
    "to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at";

template <typename RowType>
File FileFromRow(const RowType& row) {
  File file;
  file.id = row["id"].template as<std::string>();
  file.owner_id = row["owner_id"].template as<std::string>();
  file.folder_id = row["folder_id"].template as<std::string>();
  if (!row["current_version_id"].is_null()) {
    file.current_version_id = row["current_version_id"].template as<std::string>();
  }
  file.original_name = row["original_name"].template as<std::string>();
  file.mime_type = row["mime_type"].template as<std::string>();
  file.size_bytes = row["size_bytes"].template as<std::uint64_t>();
  file.content_hash = row["content_hash"].template as<std::string>();
  file.storage_key = row["storage_key"].template as<std::string>();
  file.status = ParseFileStatus(row["status"].template as<std::string>());
  file.created_at = row["created_at"].template as<std::string>();
  file.updated_at = row["updated_at"].template as<std::string>();
  if (!row["deleted_at"].is_null()) {
    file.deleted_at = row["deleted_at"].template as<std::string>();
  }
  return file;
}

template <typename RowType>
FileVersion VersionFromRow(const RowType& row) {
  FileVersion version;
  version.id = row["id"].template as<std::string>();
  version.file_id = row["file_id"].template as<std::string>();
  version.version_number = row["version_number"].template as<std::int32_t>();
  version.content_hash = row["content_hash"].template as<std::string>();
  version.size_bytes = row["size_bytes"].template as<std::uint64_t>();
  version.storage_key = row["storage_key"].template as<std::string>();
  version.created_by = row["created_by"].template as<std::string>();
  version.is_current = row["is_current"].template as<bool>();
  version.created_at = row["created_at"].template as<std::string>();
  return version;
}

}  // namespace

Result<File> FileRepository::CreateWithFirstVersion(const File& file, const FileVersion& version) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());

    // 配额检查与文件落库必须在同一个事务里，并且先锁住用户行：
    // 两个并发上传如果各自读到旧用量，就会双双通过检查（阶段六的硬要求）。
    {
      pqxx::params params;
      params.append(file.owner_id);
      const pqxx::result users = tx.exec(
          "SELECT storage_quota_bytes, used_storage_bytes FROM users "
          "WHERE id = $1::uuid FOR UPDATE",
          params);
      if (users.empty()) {
        return Error{"RESOURCE_NOT_FOUND", "用户不存在"};
      }
      const auto quota = users[0]["storage_quota_bytes"].as<std::uint64_t>();
      const auto used = users[0]["used_storage_bytes"].as<std::uint64_t>();
      // 配额按逻辑大小算：同一内容被同一用户再次引用时也要再算一份
      // （docs/data-model.md §存储统计和配额），物理去重只省磁盘。
      const std::uint64_t needed = version.size_bytes;
      if (needed > quota || used > quota - needed) {
        return Error{"QUOTA_EXCEEDED", "存储空间不足（已用 " + std::to_string(used) + " / 配额 " +
                                           std::to_string(quota) + " 字节，本次需要 " +
                                           std::to_string(needed) + " 字节）"};
      }
    }

    {
      pqxx::params params;
      params.append(file.id);
      params.append(file.owner_id);
      params.append(file.folder_id);
      params.append(file.original_name);
      params.append(file.mime_type);
      params.append(file.size_bytes);
      params.append(file.content_hash);
      params.append(file.storage_key);
      params.append(ToDbString(file.status));
      tx.exec(
          "INSERT INTO files (id, owner_id, folder_id, original_name, mime_type, size_bytes, "
          "content_hash, storage_key, status) "
          "VALUES ($1::uuid, $2::uuid, $3::uuid, $4, $5, $6, $7, $8, $9)",
          params);
    }

    {
      pqxx::params params;
      params.append(version.id);
      params.append(version.file_id);
      params.append(version.version_number);
      params.append(version.content_hash);
      params.append(version.size_bytes);
      params.append(version.storage_key);
      params.append(version.created_by);
      params.append(version.is_current);
      tx.exec(
          "INSERT INTO file_versions (id, file_id, version_number, content_hash, size_bytes, "
          "storage_key, created_by, is_current) "
          "VALUES ($1::uuid, $2::uuid, $3, $4, $5, $6, $7::uuid, $8)",
          params);
    }

    // 统计字段在同事务内累加：锁已经持有，不会与并发上传相互覆盖
    {
      pqxx::params params;
      params.append(version.size_bytes);
      params.append(file.owner_id);
      tx.exec(
          "UPDATE users SET used_storage_bytes = used_storage_bytes + $1, updated_at = now() "
          "WHERE id = $2::uuid",
          params);
    }

    const pqxx::result rows = [&] {
      pqxx::params params;
      params.append(version.id);
      params.append(file.id);
      return tx.exec(
          "UPDATE files SET current_version_id = $1::uuid, updated_at = now() "
          "WHERE id = $2::uuid RETURNING " +
              std::string(kFileColumns),
          params);
    }();

    tx.commit();

    if (rows.empty()) {
      return Error{"INTERNAL_ERROR", "创建文件未返回记录"};
    }
    return FileFromRow(rows[0]);
  } catch (const pqxx::unique_violation&) {
    return Error{"RESOURCE_CONFLICT", "同一目录下已存在同名文件"};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "创建文件失败");
  }
}

Result<std::optional<File>> FileRepository::FindById(std::string_view id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(id));
    const pqxx::result rows =
        tx.exec("SELECT " + std::string(kFileColumns) + " FROM files WHERE id = $1::uuid", params);
    tx.commit();
    if (rows.empty()) {
      return std::optional<File>{};
    }
    return std::optional<File>{FileFromRow(rows[0])};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询文件失败");
  }
}

Result<bool> FileRepository::ExistsWithName(std::string_view folder_id, std::string_view name) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(folder_id));
    params.append(std::string(name));
    const pqxx::result rows = tx.exec(
        "SELECT 1 FROM files WHERE folder_id = $1::uuid AND lower(original_name) = lower($2) "
        "AND status = 'ACTIVE' LIMIT 1",
        params);
    tx.commit();
    return !rows.empty();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询同名文件失败");
  }
}

Result<std::vector<File>> FileRepository::ListByFolder(std::string_view folder_id,
                                                       const Pagination& pagination,
                                                       const std::string& order_by) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(folder_id));
    params.append(pagination.page_size);
    params.append(pagination.Offset());
    // 表别名 f：排序白名单里的片段使用 f.xxx
    const pqxx::result rows =
        tx.exec("SELECT " + std::string(kFileColumns) +
                    " FROM files f WHERE f.folder_id = $1::uuid AND f.status = 'ACTIVE' ORDER BY " +
                    order_by + " LIMIT $2 OFFSET $3",
                params);
    tx.commit();

    std::vector<File> files;
    files.reserve(rows.size());
    for (const auto& row : rows) {
      files.push_back(FileFromRow(row));
    }
    return files;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询文件列表失败");
  }
}

Result<std::uint64_t> FileRepository::CountByFolder(std::string_view folder_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(folder_id));
    const pqxx::result rows = tx.exec(
        "SELECT count(*)::bigint AS total FROM files "
        "WHERE folder_id = $1::uuid AND status = 'ACTIVE'",
        params);
    tx.commit();
    if (rows.empty())
      return static_cast<std::uint64_t>(0);
    return rows[0]["total"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计文件失败");
  }
}

Result<std::vector<FileVersion>> FileRepository::ListVersions(std::string_view file_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(file_id));
    const pqxx::result rows =
        tx.exec("SELECT " + std::string(kVersionColumns) +
                    " FROM file_versions WHERE file_id = $1::uuid ORDER BY version_number DESC",
                params);
    tx.commit();

    std::vector<FileVersion> versions;
    versions.reserve(rows.size());
    for (const auto& row : rows) {
      versions.push_back(VersionFromRow(row));
    }
    return versions;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询文件版本失败");
  }
}

Result<std::optional<std::string>> FileRepository::FindOwnerId(std::string_view id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(id));
    const pqxx::result rows =
        tx.exec("SELECT owner_id::text AS owner_id FROM files WHERE id = $1::uuid", params);
    tx.commit();
    if (rows.empty()) {
      return std::optional<std::string>{};
    }
    return std::optional<std::string>{rows[0]["owner_id"].as<std::string>()};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询文件所有者失败");
  }
}

Status FileRepository::SoftDelete(std::string_view id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(id));
    // 只把 ACTIVE 改成 TRASHED：重复删除幂等，也不会覆盖已经恢复的记录
    tx.exec(
        "UPDATE files SET status = 'TRASHED', deleted_at = now(), updated_at = now() "
        "WHERE id = $1::uuid AND status = 'ACTIVE'",
        params);
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "删除文件失败"));
  }
}

Result<File> FileRepository::AddVersion(std::string_view file_id, const FileVersion& version,
                                        std::optional<std::string> expected_version_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());

    // 1) 锁住文件行：并发的新版本请求会在这里排队，不会各自算出同一个版本号
    std::string owner_id;
    std::optional<std::string> current_version_id;
    {
      pqxx::params params;
      params.append(std::string(file_id));
      const pqxx::result rows = tx.exec(
          "SELECT owner_id::text AS owner_id, current_version_id::text AS current_version_id "
          "FROM files WHERE id = $1::uuid FOR UPDATE",
          params);
      if (rows.empty()) {
        return Error{"FILE_NOT_FOUND", "文件不存在"};
      }
      owner_id = rows[0]["owner_id"].as<std::string>();
      if (!rows[0]["current_version_id"].is_null()) {
        current_version_id = rows[0]["current_version_id"].as<std::string>();
      }
    }

    // 2) If-Match：客户端声明的版本必须是当前版本，否则拒绝（不能静默覆盖）
    if (expected_version_id.has_value()) {
      const bool matches =
          current_version_id.has_value() && *current_version_id == *expected_version_id;
      if (!matches) {
        return Error::WithDetails("FILE_VERSION_CONFLICT", "文件已被其他人修改，请刷新后重试",
                                  {{"current_version_id", current_version_id.value_or("")}});
      }
    }

    // 3) 配额：与新建文件同一套判定（锁用户行 → 比对 → 通过后累加）
    {
      pqxx::params params;
      params.append(owner_id);
      const pqxx::result users = tx.exec(
          "SELECT storage_quota_bytes, used_storage_bytes FROM users "
          "WHERE id = $1::uuid FOR UPDATE",
          params);
      if (users.empty()) {
        return Error{"RESOURCE_NOT_FOUND", "用户不存在"};
      }
      const auto quota = users[0]["storage_quota_bytes"].as<std::uint64_t>();
      const auto used = users[0]["used_storage_bytes"].as<std::uint64_t>();
      if (version.size_bytes > quota || used > quota - version.size_bytes) {
        return Error{"QUOTA_EXCEEDED", "存储空间不足（已用 " + std::to_string(used) + " / 配额 " +
                                           std::to_string(quota) + " 字节，本版本需要 " +
                                           std::to_string(version.size_bytes) + " 字节）"};
      }
    }

    // 4) 版本号由服务端决定：当前最大值 + 1，只增不减
    std::int32_t next_number = 1;
    {
      pqxx::params params;
      params.append(std::string(file_id));
      const pqxx::result rows = tx.exec(
          "SELECT COALESCE(MAX(version_number), 0) + 1 AS next_number FROM file_versions "
          "WHERE file_id = $1::uuid",
          params);
      next_number = rows[0]["next_number"].as<std::int32_t>();
    }

    // 5) 先摘掉旧版本的 is_current（唯一索引只允许一个当前版本），再插入新版本
    {
      pqxx::params params;
      params.append(std::string(file_id));
      tx.exec(
          "UPDATE file_versions SET is_current = false "
          "WHERE file_id = $1::uuid AND is_current",
          params);
    }

    FileVersion stored = version;
    stored.file_id = std::string(file_id);
    stored.version_number = next_number;
    stored.is_current = true;
    {
      pqxx::params params;
      params.append(stored.id);
      params.append(stored.file_id);
      params.append(stored.version_number);
      params.append(stored.content_hash);
      params.append(stored.size_bytes);
      params.append(stored.storage_key);
      params.append(stored.created_by);
      params.append(stored.is_current);
      tx.exec(
          "INSERT INTO file_versions (id, file_id, version_number, content_hash, size_bytes, "
          "storage_key, created_by, is_current) "
          "VALUES ($1::uuid, $2::uuid, $3, $4, $5, $6, $7::uuid, $8)",
          params);
    }

    // 6) 文件自身指向新版本；对象 Key 与大小、哈希一并跟到最新版本
    const pqxx::result rows = [&] {
      pqxx::params params;
      params.append(stored.id);
      params.append(stored.content_hash);
      params.append(stored.size_bytes);
      params.append(stored.storage_key);
      params.append(stored.file_id);
      return tx.exec(
          "UPDATE files SET current_version_id = $1::uuid, content_hash = $2, size_bytes = $3, "
          "storage_key = $4, updated_at = now() WHERE id = $5::uuid RETURNING " +
              std::string(kFileColumns),
          params);
    }();

    // 7) 统计累加（历史版本也占配额，见 docs/data-model.md）
    {
      pqxx::params params;
      params.append(stored.size_bytes);
      params.append(owner_id);
      tx.exec(
          "UPDATE users SET used_storage_bytes = used_storage_bytes + $1, updated_at = now() "
          "WHERE id = $2::uuid",
          params);
    }

    tx.commit();
    if (rows.empty()) {
      return Error{"FILE_NOT_FOUND", "文件不存在"};
    }
    return FileFromRow(rows[0]);
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "创建文件版本失败");
  }
}

Result<std::optional<FileVersion>> FileRepository::FindVersion(std::string_view file_id,
                                                               std::string_view version_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(version_id));
    params.append(std::string(file_id));
    const pqxx::result rows = tx.exec("SELECT " + std::string(kVersionColumns) +
                                          " FROM file_versions WHERE id = $1::uuid AND "
                                          "file_id = $2::uuid",
                                      params);
    tx.commit();
    if (rows.empty()) {
      return std::optional<FileVersion>{};
    }
    return std::optional<FileVersion>{VersionFromRow(rows[0])};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询文件版本失败");
  }
}

Result<std::optional<FileVersion>> FileRepository::FindCurrentVersion(std::string_view file_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(file_id));
    const pqxx::result rows = tx.exec("SELECT " + std::string(kVersionColumns) +
                                          " FROM file_versions WHERE file_id = $1::uuid AND "
                                          "is_current LIMIT 1",
                                      params);
    tx.commit();
    if (rows.empty()) {
      return std::optional<FileVersion>{};
    }
    return std::optional<FileVersion>{VersionFromRow(rows[0])};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询当前版本失败");
  }
}

Result<std::optional<ExistingObject>> FileRepository::FindOwnedObjectByHash(
    std::string_view owner_id, std::string_view content_hash) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(owner_id));
    params.append(std::string(content_hash));
    // 只查自己的文件：上传接口不回答「系统里有没有这个哈希」
    const pqxx::result rows = tx.exec(
        "SELECT f.id::text AS file_id, f.original_name, v.content_hash, v.size_bytes, "
        "v.storage_key "
        "FROM file_versions v JOIN files f ON f.id = v.file_id "
        "WHERE f.owner_id = $1::uuid AND v.content_hash = $2 "
        "ORDER BY v.created_at DESC LIMIT 1",
        params);
    tx.commit();

    if (rows.empty()) {
      return std::optional<ExistingObject>{};
    }
    ExistingObject object;
    object.file_id = rows[0]["file_id"].as<std::string>();
    object.file_name = rows[0]["original_name"].as<std::string>();
    object.content_hash = rows[0]["content_hash"].as<std::string>();
    object.size_bytes = rows[0]["size_bytes"].as<std::uint64_t>();
    object.storage_key = rows[0]["storage_key"].as<std::string>();
    return std::optional<ExistingObject>{std::move(object)};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询已有对象失败");
  }
}

Result<std::uint64_t> FileRepository::CountObjectReferences(std::string_view content_hash) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(content_hash));
    // 引用计数直接由版本表推导，没有单独计数器可用性漂移的问题
    const pqxx::result rows = tx.exec(
        "SELECT count(*)::bigint AS total FROM file_versions WHERE content_hash = $1", params);
    tx.commit();
    if (rows.empty()) {
      return static_cast<std::uint64_t>(0);
    }
    return rows[0]["total"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计对象引用失败");
  }
}

Result<std::uint64_t> FileRepository::CountAllFiles() {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    const pqxx::result rows = tx.exec("SELECT count(*)::bigint AS total FROM files");
    tx.commit();
    return rows.empty() ? static_cast<std::uint64_t>(0) : rows[0]["total"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计文件总数失败");
  }
}

Result<std::uint64_t> FileRepository::CountAllVersions() {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    const pqxx::result rows = tx.exec("SELECT count(*)::bigint AS total FROM file_versions");
    tx.commit();
    return rows.empty() ? static_cast<std::uint64_t>(0) : rows[0]["total"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计版本总数失败");
  }
}
Result<std::uint64_t> FileRepository::SumAllVersionBytes() {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    // 版本行还在，物理对象就还在（永久删除会同时删行），所以按版本表求和才是真实占用
    const pqxx::result rows =
        tx.exec("SELECT COALESCE(SUM(size_bytes), 0)::bigint AS total FROM file_versions");
    tx.commit();
    if (rows.empty()) {
      return static_cast<std::uint64_t>(0);
    }
    return rows[0]["total"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计系统存储用量失败");
  }
}
Result<std::vector<std::string>> FileRepository::ListDistinctContentHashes(int limit) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(limit);
    const pqxx::result rows = tx.exec(
        "SELECT DISTINCT content_hash FROM file_versions ORDER BY content_hash LIMIT $1", params);
    tx.commit();
    std::vector<std::string> hashes;
    hashes.reserve(rows.size());
    for (const auto& row : rows) {
      hashes.push_back(row["content_hash"].as<std::string>());
    }
    return hashes;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "列出被引用的内容哈希失败");
  }
}
Result<std::uint64_t> FileRepository::RecalculateAllUsedStorage() {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    // 一条语句完成：先算出每个用户的真实用量，再只更新**有偏差**的那些行。
    // 只改不一致的行，既省写入，也让返回的"修正数量"成为可观测的偏差信号。
    const pqxx::result rows = tx.exec(
        "WITH actual AS ("
        "  SELECT u.id AS user_id, COALESCE(SUM(v.size_bytes), 0)::bigint AS used"
        "  FROM users u"
        "  LEFT JOIN files f ON f.owner_id = u.id"
        "  LEFT JOIN file_versions v ON v.file_id = f.id"
        "  GROUP BY u.id"
        ") "
        "UPDATE users SET used_storage_bytes = a.used, updated_at = now() "
        "FROM actual a "
        "WHERE users.id = a.user_id AND users.used_storage_bytes <> a.used "
        "RETURNING users.id");
    tx.commit();
    return static_cast<std::uint64_t>(rows.size());
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "校准用户存储统计失败");
  }
}
Result<std::vector<FileRepository::RecentFile>> FileRepository::ListRecentByOwner(
    std::string_view owner_id, int limit, int offset) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(owner_id));
    params.append(limit);
    params.append(offset);
    // ⛔ 不要 JOIN folders：kFileColumns 里的列名没有表别名（`id, owner_id, ...`），
    //    join 进来另一张也有 `id` 的表会直接报「字段关联 "id" 是不明确的」（实测踩到）。
    //    用相关子查询取文件夹名，既不歧义也不用改公共的 kFileColumns。
    const pqxx::result rows = tx.exec(
        "SELECT " + std::string(kFileColumns) +
            ", COALESCE((SELECT name FROM folders WHERE folders.id = f.folder_id), '') "
            "  AS folder_name "
            "FROM files f "
            "WHERE f.owner_id = $1::uuid AND f.status = 'ACTIVE' "
            "ORDER BY f.updated_at DESC, f.id LIMIT $2 OFFSET $3",
        params);
    tx.commit();

    std::vector<RecentFile> recent;
    recent.reserve(rows.size());
    for (const auto& row : rows) {
      RecentFile item;
      item.file = FileFromRow(row);
      item.folder_id = item.file.folder_id;
      item.folder_name = row["folder_name"].as<std::string>();
      recent.push_back(std::move(item));
    }
    return recent;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询最近文件失败");
  }
}

Result<std::uint64_t> FileRepository::CountRecentByOwner(std::string_view owner_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(owner_id));
    const pqxx::result rows = tx.exec(
        "SELECT count(*)::bigint AS total FROM files WHERE owner_id = $1::uuid "
        "AND status = 'ACTIVE'",
        params);
    tx.commit();
    return static_cast<std::uint64_t>(rows[0]["total"].as<std::int64_t>());
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计最近文件失败");
  }
}

Result<std::uint64_t> FileRepository::SumSizeByOwner(std::string_view owner_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(owner_id));
    // 与 users.used_storage_bytes 的口径保持一致：
    // 该用户所有文件版本大小之和（含回收站与历史版本，docs/data-model.md §存储统计和配额）。
    // 直接按 files.size_bytes 汇总会漏掉历史版本，阶段七起就会算错。
    const pqxx::result rows = tx.exec(
        "SELECT COALESCE(sum(v.size_bytes), 0)::bigint AS total "
        "FROM file_versions v JOIN files f ON f.id = v.file_id "
        "WHERE f.owner_id = $1::uuid",
        params);
    tx.commit();
    if (rows.empty())
      return static_cast<std::uint64_t>(0);
    return rows[0]["total"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计用户用量失败");
  }
}

Status FileRepository::DeleteByIdForTesting(std::string_view file_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(file_id));
    const pqxx::result rows = tx.exec("DELETE FROM files WHERE id = $1::uuid RETURNING id", params);
    tx.commit();
    if (rows.empty()) {
      return Status(Error{"FILE_NOT_FOUND", "文件不存在"});
    }
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "删除文件记录失败"));
  }
}

Status FileRepository::DeleteAllForTesting() {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    tx.exec("DELETE FROM files");
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "清空 files 表失败"));
  }
}

}  // namespace lan_file_hub
