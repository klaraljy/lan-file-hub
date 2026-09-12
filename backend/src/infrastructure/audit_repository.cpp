#include "lan_file_hub/infrastructure/audit_repository.h"

#include <pqxx/pqxx>
#include <spdlog/spdlog.h>

#include "lan_file_hub/common/uuid.h"

#include "infrastructure/database_error.h"

namespace lan_file_hub {
namespace {

constexpr char kAuditColumns[] =
    "id, actor_id, action, resource_type, resource_id, detail::text AS detail, "
    "to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at";

constexpr char kCleanupColumns[] =
    "id, storage_key, reason, attempts, status, last_error, "
    "to_char(next_attempt_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') "
    "AS next_attempt_at, "
    "to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at";

template <typename RowType>
AuditEntry AuditFromRow(const RowType& row) {
  AuditEntry entry;
  entry.id = row["id"].template as<std::string>();
  if (!row["actor_id"].is_null()) {
    entry.actor_id = row["actor_id"].template as<std::string>();
  }
  entry.action = row["action"].template as<std::string>();
  entry.resource_type = row["resource_type"].template as<std::string>();
  if (!row["resource_id"].is_null()) {
    entry.resource_id = row["resource_id"].template as<std::string>();
  }
  entry.detail_json = row["detail"].template as<std::string>();
  entry.created_at = row["created_at"].template as<std::string>();
  return entry;
}

template <typename RowType>
CleanupTask CleanupFromRow(const RowType& row) {
  CleanupTask task;
  task.id = row["id"].template as<std::string>();
  task.storage_key = row["storage_key"].template as<std::string>();
  task.reason = row["reason"].template as<std::string>();
  task.attempts = row["attempts"].template as<std::int32_t>();
  const std::string status = row["status"].template as<std::string>();
  task.status = status == "DONE"     ? CleanupStatus::kDone
                : status == "FAILED" ? CleanupStatus::kFailed
                                     : CleanupStatus::kPending;
  if (!row["last_error"].is_null()) {
    task.last_error = row["last_error"].template as<std::string>();
  }
  task.next_attempt_at = row["next_attempt_at"].template as<std::string>();
  task.created_at = row["created_at"].template as<std::string>();
  return task;
}

}  // namespace

std::string ToDbString(CleanupStatus status) {
  switch (status) {
    case CleanupStatus::kDone:
      return "DONE";
    case CleanupStatus::kFailed:
      return "FAILED";
    case CleanupStatus::kPending:
    default:
      return "PENDING";
  }
}

// ---- AuditLogRepository -----------------------------------------------------

Status AuditLogRepository::Record(const AuditEntry& entry) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(entry.id.empty() ? GenerateUuidV4() : entry.id);
    if (entry.actor_id.empty()) {
      params.append(nullptr);
    } else {
      params.append(entry.actor_id);
    }
    params.append(entry.action);
    params.append(entry.resource_type);
    if (entry.resource_id.empty()) {
      params.append(nullptr);
    } else {
      params.append(entry.resource_id);
    }
    params.append(entry.detail_json.empty() ? std::string("{}") : entry.detail_json);
    tx.exec(
        "INSERT INTO audit_logs (id, actor_id, action, resource_type, resource_id, detail) "
        "VALUES ($1::uuid, $2::uuid, $3, $4, $5::uuid, $6::jsonb)",
        params);
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    // 审计写入失败不能让业务回滚，但必须留下痕迹
    return Status(DatabaseError(ex, "写入审计日志失败"));
  }
}

Result<std::vector<AuditEntry>> AuditLogRepository::ListRecent(std::string_view resource_id,
                                                               int limit) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(resource_id));
    params.append(limit);
    const pqxx::result rows = tx.exec("SELECT " + std::string(kAuditColumns) +
                                          " FROM audit_logs WHERE resource_id = $1::uuid "
                                          "ORDER BY created_at DESC LIMIT $2",
                                      params);
    tx.commit();
    std::vector<AuditEntry> entries;
    entries.reserve(rows.size());
    for (const auto& row : rows) {
      entries.push_back(AuditFromRow(row));
    }
    return entries;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询审计日志失败");
  }
}

Result<std::uint64_t> AuditLogRepository::CountByAction(std::string_view action) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(action));
    const pqxx::result rows =
        tx.exec("SELECT count(*)::bigint AS total FROM audit_logs WHERE action = $1", params);
    tx.commit();
    if (rows.empty()) {
      return static_cast<std::uint64_t>(0);
    }
    return rows[0]["total"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计审计日志失败");
  }
}

namespace {

// 审计查询的 WHERE 片段：过滤字段是**编译期固定**的片段，值一律走绑定参数
std::string AuditWhere(const AuditLogRepository::AuditQuery& query, pqxx::params& params) {
  std::string where = " WHERE 1=1";
  if (!query.actor_id.empty()) {
    params.append(query.actor_id);
    where += " AND actor_id = $" + std::to_string(params.size()) + "::uuid";
  }
  if (!query.action.empty()) {
    params.append(query.action);
    where += " AND action = $" + std::to_string(params.size());
  }
  if (!query.resource_type.empty()) {
    params.append(query.resource_type);
    where += " AND resource_type = $" + std::to_string(params.size());
  }
  if (!query.resource_id.empty()) {
    params.append(query.resource_id);
    where += " AND resource_id = $" + std::to_string(params.size()) + "::uuid";
  }
  return where;
}

}  // namespace

Result<std::vector<AuditEntry>> AuditLogRepository::List(const AuditQuery& query, int limit,
                                                         int offset) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    const std::string where = AuditWhere(query, params);
    params.append(limit);
    params.append(offset);
    const pqxx::result rows =
        tx.exec("SELECT " + std::string(kAuditColumns) + " FROM audit_logs" + where +
                    " ORDER BY created_at DESC LIMIT $" + std::to_string(params.size() - 1) +
                    " OFFSET $" + std::to_string(params.size()),
                params);
    tx.commit();
    std::vector<AuditEntry> entries;
    entries.reserve(rows.size());
    for (const auto& row : rows) {
      entries.push_back(AuditFromRow(row));
    }
    return entries;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询审计日志失败");
  }
}

Result<std::uint64_t> AuditLogRepository::Count(const AuditQuery& query) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    const std::string where = AuditWhere(query, params);
    const pqxx::result rows =
        tx.exec("SELECT count(*)::bigint AS total FROM audit_logs" + where, params);
    tx.commit();
    if (rows.empty()) {
      return static_cast<std::uint64_t>(0);
    }
    return rows[0]["total"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计审计日志失败");
  }
}
Status AuditLogRepository::DeleteAllForTesting() {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    tx.exec("DELETE FROM audit_logs");
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "清空 audit_logs 表失败"));
  }
}

// ---- CleanupTaskRepository --------------------------------------------------

Status CleanupTaskRepository::Enqueue(std::string_view storage_key, std::string_view reason) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(GenerateUuidV4());
    params.append(std::string(storage_key));
    params.append(std::string(reason));
    // 同一个 Key 只保留一条待处理任务：重复删除请求不会堆出一串重复任务
    tx.exec(
        "INSERT INTO cleanup_tasks (id, storage_key, reason, status) "
        "VALUES ($1::uuid, $2, $3, 'PENDING') "
        "ON CONFLICT (storage_key) WHERE status = 'PENDING' DO NOTHING",
        params);
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "创建清理任务失败"));
  }
}

Status CleanupTaskRepository::EnqueueMany(const std::vector<std::string>& storage_keys,
                                          std::string_view reason) {
  if (storage_keys.empty()) {
    return Status::Success();
  }
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    for (const auto& key : storage_keys) {
      pqxx::params params;
      params.append(GenerateUuidV4());
      params.append(key);
      params.append(std::string(reason));
      tx.exec(
          "INSERT INTO cleanup_tasks (id, storage_key, reason, status) "
          "VALUES ($1::uuid, $2, $3, 'PENDING') "
          "ON CONFLICT (storage_key) WHERE status = 'PENDING' DO NOTHING",
          params);
    }
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "批量创建清理任务失败"));
  }
}

Result<std::vector<CleanupTask>> CleanupTaskRepository::ClaimDue(int limit) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(limit);
    // SKIP LOCKED：多个 worker 并发时各取各的，不会互相等待
    const pqxx::result rows = tx.exec(
        "UPDATE cleanup_tasks SET attempts = attempts + 1, updated_at = now() "
        "WHERE id IN ("
        "  SELECT id FROM cleanup_tasks WHERE status = 'PENDING' AND next_attempt_at <= now() "
        "  ORDER BY next_attempt_at ASC, created_at ASC LIMIT $1 FOR UPDATE SKIP LOCKED"
        ") RETURNING " +
            std::string(kCleanupColumns),
        params);
    tx.commit();
    std::vector<CleanupTask> tasks;
    tasks.reserve(rows.size());
    for (const auto& row : rows) {
      tasks.push_back(CleanupFromRow(row));
    }
    return tasks;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "领取清理任务失败");
  }
}

Status CleanupTaskRepository::MarkDone(std::string_view id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(id));
    tx.exec(
        "UPDATE cleanup_tasks SET status = 'DONE', last_error = NULL, updated_at = now() "
        "WHERE id = $1::uuid",
        params);
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "标记清理任务完成失败"));
  }
}

Status CleanupTaskRepository::MarkFailed(std::string_view id, std::string_view error,
                                         std::int32_t max_attempts) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(error));
    params.append(max_attempts);
    params.append(std::string(id));
    // 退避：1s、2s、4s…（上限约 17 分钟），超过次数上限置为 FAILED 等人工处理
    tx.exec(
        "UPDATE cleanup_tasks SET "
        "  status = CASE WHEN attempts >= $2 THEN 'FAILED' ELSE 'PENDING' END, "
        "  last_error = $1, "
        "  next_attempt_at = now() + make_interval(secs => LEAST(power(2, attempts)::int, 1024)), "
        "  updated_at = now() "
        "WHERE id = $3::uuid",
        params);
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "标记清理任务失败状态失败"));
  }
}

Result<std::uint64_t> CleanupTaskRepository::CountByStatus(CleanupStatus status) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(ToDbString(status));
    const pqxx::result rows =
        tx.exec("SELECT count(*)::bigint AS total FROM cleanup_tasks WHERE status = $1", params);
    tx.commit();
    if (rows.empty()) {
      return static_cast<std::uint64_t>(0);
    }
    return rows[0]["total"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计清理任务失败");
  }
}

Result<std::vector<CleanupTask>> CleanupTaskRepository::ListByStatus(CleanupStatus status,
                                                                     int limit) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(ToDbString(status));
    params.append(limit);
    const pqxx::result rows =
        tx.exec("SELECT " + std::string(kCleanupColumns) +
                    " FROM cleanup_tasks WHERE status = $1 ORDER BY created_at ASC LIMIT $2",
                params);
    tx.commit();
    std::vector<CleanupTask> tasks;
    tasks.reserve(rows.size());
    for (const auto& row : rows) {
      tasks.push_back(CleanupFromRow(row));
    }
    return tasks;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询清理任务失败");
  }
}

Status CleanupTaskRepository::ResetPendingForStartup() {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    const pqxx::result rows = tx.exec(
        "UPDATE cleanup_tasks SET next_attempt_at = now(), updated_at = now() "
        "WHERE status = 'PENDING' AND next_attempt_at > now() RETURNING id");
    tx.commit();
    if (!rows.empty()) {
      spdlog::warn("启动恢复：{} 个待处理清理任务被立即重新排队", rows.size());
    }
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "重置清理任务时间失败"));
  }
}

Status CleanupTaskRepository::DeleteAllForTesting() {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    tx.exec("DELETE FROM cleanup_tasks");
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "清空 cleanup_tasks 表失败"));
  }
}

}  // namespace lan_file_hub
