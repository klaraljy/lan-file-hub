#include "lan_file_hub/infrastructure/upload_session_repository.h"

#include <pqxx/pqxx>

#include "infrastructure/database_error.h"

namespace lan_file_hub {
namespace {

constexpr char kSessionColumns[] =
    "id, user_id, folder_id, file_name, mime_type, total_size, chunk_size, total_chunks, "
    "expected_hash, status, file_id, target_file_id, expected_version_id, error_code, "
    "to_char(expires_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS expires_at, "
    "to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at, "
    "to_char(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS updated_at";

template <typename RowType>
UploadSession SessionFromRow(const RowType& row) {
  UploadSession session;
  session.id = row["id"].template as<std::string>();
  session.user_id = row["user_id"].template as<std::string>();
  session.folder_id = row["folder_id"].template as<std::string>();
  session.file_name = row["file_name"].template as<std::string>();
  session.mime_type = row["mime_type"].template as<std::string>();
  session.total_size = row["total_size"].template as<std::uint64_t>();
  session.chunk_size = row["chunk_size"].template as<std::uint32_t>();
  session.total_chunks = row["total_chunks"].template as<std::uint32_t>();
  if (!row["expected_hash"].is_null()) {
    session.expected_hash = row["expected_hash"].template as<std::string>();
  }
  session.status = ParseUploadStatus(row["status"].template as<std::string>());
  if (!row["file_id"].is_null()) {
    session.file_id = row["file_id"].template as<std::string>();
  }
  if (!row["target_file_id"].is_null()) {
    session.target_file_id = row["target_file_id"].template as<std::string>();
  }
  if (!row["expected_version_id"].is_null()) {
    session.expected_version_id = row["expected_version_id"].template as<std::string>();
  }
  if (!row["error_code"].is_null()) {
    session.error_code = row["error_code"].template as<std::string>();
  }
  session.expires_at = row["expires_at"].template as<std::string>();
  session.created_at = row["created_at"].template as<std::string>();
  session.updated_at = row["updated_at"].template as<std::string>();
  return session;
}

}  // namespace

Result<UploadSession> UploadSessionRepository::Create(const UploadSession& session,
                                                      std::uint32_t ttl_seconds) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(session.id);
    params.append(session.user_id);
    params.append(session.folder_id);
    params.append(session.file_name);
    params.append(session.mime_type);
    params.append(session.total_size);
    params.append(static_cast<int>(session.chunk_size));
    params.append(static_cast<int>(session.total_chunks));
    if (session.expected_hash.has_value()) {
      params.append(*session.expected_hash);
    } else {
      params.append(nullptr);
    }
    params.append(ToDbString(session.status));
    if (session.target_file_id.has_value()) {
      params.append(*session.target_file_id);
    } else {
      params.append(nullptr);
    }
    if (session.expected_version_id.has_value()) {
      params.append(*session.expected_version_id);
    } else {
      params.append(nullptr);
    }
    params.append(static_cast<int>(ttl_seconds));

    const pqxx::result rows = tx.exec(
        "INSERT INTO upload_sessions (id, user_id, folder_id, file_name, mime_type, total_size, "
        "chunk_size, total_chunks, expected_hash, status, target_file_id, expected_version_id, "
        "expires_at) "
        "VALUES ($1::uuid, $2::uuid, $3::uuid, $4, $5, $6, $7, $8, $9, $10, $11::uuid, "
        "$12::uuid, now() + make_interval(secs => $13)) "
        "RETURNING " +
            std::string(kSessionColumns),
        params);
    tx.commit();

    if (rows.empty()) {
      return Error{"INTERNAL_ERROR", "创建上传会话未返回记录"};
    }
    return SessionFromRow(rows[0]);
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "创建上传会话失败");
  }
}

Result<std::optional<UploadSession>> UploadSessionRepository::FindById(std::string_view id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());

    // 顺带把已过期的进行中会话落成 EXPIRED：过期判断交给数据库的 now()，
    // 避免在 C++ 里做时间解析。
    {
      pqxx::params params;
      params.append(std::string(id));
      tx.exec(
          "UPDATE upload_sessions SET status = 'EXPIRED', updated_at = now() "
          "WHERE id = $1::uuid AND status IN ('INITIATED', 'UPLOADING') AND expires_at < now()",
          params);
    }

    pqxx::params params;
    params.append(std::string(id));
    const pqxx::result rows = tx.exec(
        "SELECT " + std::string(kSessionColumns) + " FROM upload_sessions WHERE id = $1::uuid",
        params);
    tx.commit();

    if (rows.empty()) {
      return std::optional<UploadSession>{};
    }
    return std::optional<UploadSession>{SessionFromRow(rows[0])};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询上传会话失败");
  }
}

Result<bool> UploadSessionRepository::ClaimForMerge(std::string_view id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(id));
    // compare-and-set：只有第一个请求能把状态推进到 MERGING
    const pqxx::result rows = tx.exec(
        "UPDATE upload_sessions SET status = 'MERGING', updated_at = now() "
        "WHERE id = $1::uuid AND status IN ('INITIATED', 'UPLOADING') RETURNING id",
        params);
    tx.commit();
    return !rows.empty();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "抢占上传会话失败");
  }
}

Status UploadSessionRepository::UpdateStatus(std::string_view id, UploadStatus status,
                                             std::optional<std::string> error_code) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(ToDbString(status));
    if (error_code.has_value()) {
      params.append(*error_code);
    } else {
      params.append(nullptr);
    }
    params.append(std::string(id));
    const pqxx::result rows = tx.exec(
        "UPDATE upload_sessions SET status = $1, error_code = $2, updated_at = now() "
        "WHERE id = $3::uuid RETURNING id",
        params);
    tx.commit();
    if (rows.empty()) {
      return Status(Error{"UPLOAD_NOT_FOUND", "上传会话不存在"});
    }
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "更新上传会话状态失败"));
  }
}

Status UploadSessionRepository::AttachFileAndComplete(std::string_view id,
                                                      std::string_view file_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(file_id));
    params.append(std::string(id));
    const pqxx::result rows = tx.exec(
        "UPDATE upload_sessions SET status = 'COMPLETED', file_id = $1::uuid, error_code = NULL, "
        "updated_at = now() WHERE id = $2::uuid RETURNING id",
        params);
    if (rows.empty()) {
      tx.commit();
      return Status(Error{"UPLOAD_NOT_FOUND", "上传会话不存在"});
    }
    // 分块记录与数据库行一起清理，保持一致
    pqxx::params chunk_params;
    chunk_params.append(std::string(id));
    tx.exec("DELETE FROM upload_chunks WHERE upload_session_id = $1::uuid", chunk_params);
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "标记上传完成失败"));
  }
}

Result<ChunkUpsertResult> UploadSessionRepository::UpsertChunk(const UploadChunk& chunk) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());

    {
      pqxx::params params;
      params.append(chunk.upload_session_id);
      params.append(static_cast<int>(chunk.chunk_index));
      params.append(chunk.size_bytes);
      params.append(chunk.content_hash);
      params.append(chunk.storage_key);
      const pqxx::result inserted = tx.exec(
          "INSERT INTO upload_chunks (upload_session_id, chunk_index, size_bytes, content_hash, "
          "storage_key) VALUES ($1::uuid, $2, $3, $4, $5) "
          "ON CONFLICT (upload_session_id, chunk_index) DO NOTHING RETURNING chunk_index",
          params);
      if (!inserted.empty()) {
        tx.commit();
        return ChunkUpsertResult::kInserted;
      }
    }

    // 已经存在：内容一致就是幂等成功，不一致必须报冲突
    pqxx::params params;
    params.append(chunk.upload_session_id);
    params.append(static_cast<int>(chunk.chunk_index));
    const pqxx::result existing = tx.exec(
        "SELECT content_hash FROM upload_chunks WHERE upload_session_id = $1::uuid AND "
        "chunk_index = $2",
        params);
    tx.commit();

    if (existing.empty()) {
      return Error{"INTERNAL_ERROR", "分块写入状态异常"};
    }
    const std::string stored = existing[0]["content_hash"].as<std::string>();
    return stored == chunk.content_hash ? ChunkUpsertResult::kAlreadyPresent
                                        : ChunkUpsertResult::kConflict;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "写入分块记录失败");
  }
}

Result<std::optional<UploadChunk>> UploadSessionRepository::FindChunk(std::string_view session_id,
                                                                      std::uint32_t chunk_index) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(session_id));
    params.append(static_cast<int>(chunk_index));
    const pqxx::result rows = tx.exec(
        "SELECT upload_session_id::text AS upload_session_id, chunk_index, size_bytes, "
        "content_hash, storage_key, "
        "to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at "
        "FROM upload_chunks WHERE upload_session_id = $1::uuid AND chunk_index = $2",
        params);
    tx.commit();
    if (rows.empty()) {
      return std::optional<UploadChunk>{};
    }
    const auto& row = rows[0];
    UploadChunk chunk;
    chunk.upload_session_id = row["upload_session_id"].as<std::string>();
    chunk.chunk_index = row["chunk_index"].as<std::uint32_t>();
    chunk.size_bytes = row["size_bytes"].as<std::uint64_t>();
    chunk.content_hash = row["content_hash"].as<std::string>();
    chunk.storage_key = row["storage_key"].as<std::string>();
    chunk.created_at = row["created_at"].as<std::string>();
    return std::optional<UploadChunk>{std::move(chunk)};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询分块记录失败");
  }
}

Result<std::vector<std::uint32_t>> UploadSessionRepository::ListChunkIndexes(
    std::string_view session_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(session_id));
    const pqxx::result rows = tx.exec(
        "SELECT chunk_index FROM upload_chunks WHERE upload_session_id = $1::uuid "
        "ORDER BY chunk_index ASC",
        params);
    tx.commit();

    std::vector<std::uint32_t> indexes;
    indexes.reserve(rows.size());
    for (const auto& row : rows) {
      indexes.push_back(row["chunk_index"].as<std::uint32_t>());
    }
    return indexes;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询分块列表失败");
  }
}

Result<std::uint64_t> UploadSessionRepository::CountChunks(std::string_view session_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(session_id));
    const pqxx::result rows = tx.exec(
        "SELECT count(*)::bigint AS total FROM upload_chunks WHERE upload_session_id = $1::uuid",
        params);
    tx.commit();
    if (rows.empty())
      return static_cast<std::uint64_t>(0);
    return rows[0]["total"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计分块失败");
  }
}

Result<std::uint64_t> UploadSessionRepository::SumChunkBytes(std::string_view session_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(session_id));
    const pqxx::result rows = tx.exec(
        "SELECT COALESCE(sum(size_bytes), 0)::bigint AS total FROM upload_chunks "
        "WHERE upload_session_id = $1::uuid",
        params);
    tx.commit();
    if (rows.empty())
      return static_cast<std::uint64_t>(0);
    return rows[0]["total"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计分块大小失败");
  }
}

Result<std::uint64_t> UploadSessionRepository::CountInProgressByUser(std::string_view user_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(user_id));
    const pqxx::result rows = tx.exec(
        "SELECT count(*)::bigint AS total FROM upload_sessions "
        "WHERE user_id = $1::uuid AND status IN ('INITIATED', 'UPLOADING') AND expires_at > now()",
        params);
    tx.commit();
    if (rows.empty())
      return static_cast<std::uint64_t>(0);
    return rows[0]["total"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计用户上传会话失败");
  }
}

Result<std::uint64_t> UploadSessionRepository::CountInProgressGlobal() {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    const pqxx::result rows = tx.exec(
        "SELECT count(*)::bigint AS total FROM upload_sessions "
        "WHERE status IN ('INITIATED', 'UPLOADING') AND expires_at > now()");
    tx.commit();
    if (rows.empty())
      return static_cast<std::uint64_t>(0);
    return rows[0]["total"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计全局上传会话失败");
  }
}

Status UploadSessionRepository::DeleteChunks(std::string_view session_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(session_id));
    tx.exec("DELETE FROM upload_chunks WHERE upload_session_id = $1::uuid", params);
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "清空分块记录失败"));
  }
}

Status UploadSessionRepository::DeleteSession(std::string_view session_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(session_id));
    const pqxx::result rows =
        tx.exec("DELETE FROM upload_sessions WHERE id = $1::uuid RETURNING id", params);
    tx.commit();
    if (rows.empty()) {
      return Status(Error{"UPLOAD_NOT_FOUND", "上传会话不存在"});
    }
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "删除上传会话失败"));
  }
}

Status UploadSessionRepository::ExpireForTesting(std::string_view session_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(session_id));
    // 只改过期时间，状态仍由 FindById 按 now() 判定，走的是真实过期路径
    const pqxx::result rows = tx.exec(
        "UPDATE upload_sessions SET expires_at = now() - interval '1 second' "
        "WHERE id = $1::uuid RETURNING id",
        params);
    tx.commit();
    if (rows.empty()) {
      return Status(Error{"UPLOAD_NOT_FOUND", "上传会话不存在"});
    }
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "设置会话过期时间失败"));
  }
}

Result<std::vector<std::string>> UploadSessionRepository::ExpireOverdue(int limit) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(limit);
    // 只处理进行中的会话；终态不重复处理（幂等）
    const pqxx::result rows = tx.exec(
        "UPDATE upload_sessions SET status = 'EXPIRED', updated_at = now() "
        "WHERE id IN (SELECT id FROM upload_sessions "
        "             WHERE status IN ('INITIATED', 'UPLOADING') AND expires_at < now() "
        "             ORDER BY expires_at ASC LIMIT $1) "
        "RETURNING id::text AS id",
        params);
    tx.commit();
    std::vector<std::string> ids;
    ids.reserve(rows.size());
    for (const auto& row : rows) {
      ids.push_back(row["id"].as<std::string>());
    }
    return ids;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "过期上传会话处理失败");
  }
}

Status UploadSessionRepository::DeleteAllForTesting() {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    tx.exec("DELETE FROM upload_sessions");
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "清空 upload_sessions 表失败"));
  }
}

}  // namespace lan_file_hub
