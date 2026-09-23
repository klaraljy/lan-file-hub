#include "lan_file_hub/infrastructure/user_repository.h"

#include <array>

#include <pqxx/pqxx>

#include "infrastructure/database_error.h"

namespace lan_file_hub {
namespace {

// 统一的时间输出格式：UTC ISO8601，与 docs/api.md 的「统一时间格式」一致
constexpr char kSelectColumns[] =
    "id, username, email, password_hash, role, status, storage_quota_bytes, used_storage_bytes, "
    "presence_visible, "
    "to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at, "
    "to_char(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS updated_at";

// pqxx 8 里 result[i] 返回的是 row_ref（轻量引用）而不是 row，
// 所以这里用模板同时兼容两者，避免绑死具体类型。
// 注意：RowType 是模板参数，row["x"].as<T>() 属于依赖名，必须写 .template as<T>()。
template <typename RowType>
User UserFromRow(const RowType& row) {
  User user;
  user.id = row["id"].template as<std::string>();
  user.username = row["username"].template as<std::string>();
  user.email = row["email"].template as<std::string>();
  user.password_hash = row["password_hash"].template as<std::string>();
  user.role = ParseUserRole(row["role"].template as<std::string>());
  user.status = ParseUserStatus(row["status"].template as<std::string>());
  user.storage_quota_bytes = row["storage_quota_bytes"].template as<std::uint64_t>();
  user.used_storage_bytes = row["used_storage_bytes"].template as<std::uint64_t>();
  user.presence_visible = row["presence_visible"].template as<bool>();
  user.created_at = row["created_at"].template as<std::string>();
  user.updated_at = row["updated_at"].template as<std::string>();
  return user;
}

}  // namespace

Result<User> UserRepository::Create(const User& user) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(user.id);
    params.append(user.username);
    params.append(user.email);
    params.append(user.password_hash);
    params.append(ToDbString(user.role));
    params.append(ToDbString(user.status));
    params.append(user.storage_quota_bytes);
    params.append(user.used_storage_bytes);

    const pqxx::result rows = tx.exec(
        "INSERT INTO users (id, username, email, password_hash, role, status, "
        "storage_quota_bytes, used_storage_bytes) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8) RETURNING " +
            std::string(kSelectColumns),
        params);
    tx.commit();

    if (rows.empty()) {
      return Error{"INTERNAL_ERROR", "创建用户未返回记录"};
    }
    return UserFromRow(rows[0]);
  } catch (const pqxx::unique_violation&) {
    // 唯一索引冲突：用户名或邮箱已被占用
    return Error{"RESOURCE_CONFLICT", "用户名或邮箱已被占用"};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "创建用户失败");
  }
}

Result<std::optional<User>> UserRepository::FindByUsernameOrEmail(std::string_view identifier) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(identifier));

    const pqxx::result rows = tx.exec(
        "SELECT " + std::string(kSelectColumns) +
            " FROM users WHERE lower(username) = lower($1) OR lower(email) = lower($1) LIMIT 1",
        params);
    tx.commit();

    if (rows.empty()) {
      return std::optional<User>{};
    }
    return std::optional<User>{UserFromRow(rows[0])};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询用户失败");
  }
}

Result<std::optional<User>> UserRepository::FindById(std::string_view id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(id));

    const pqxx::result rows =
        tx.exec("SELECT " + std::string(kSelectColumns) + " FROM users WHERE id = $1::uuid LIMIT 1",
                params);
    tx.commit();

    if (rows.empty()) {
      return std::optional<User>{};
    }
    return std::optional<User>{UserFromRow(rows[0])};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询用户失败");
  }
}

Result<std::optional<User>> UserRepository::FindByUsername(std::string_view username) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(username));

    const pqxx::result rows = tx.exec("SELECT " + std::string(kSelectColumns) +
                                          " FROM users WHERE lower(username) = lower($1) LIMIT 1",
                                      params);
    tx.commit();

    if (rows.empty()) {
      return std::optional<User>{};
    }
    return std::optional<User>{UserFromRow(rows[0])};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询用户失败");
  }
}

Status UserRepository::UpdateStatus(std::string_view id, UserStatus status) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(ToDbString(status));
    params.append(std::string(id));

    const pqxx::result rows =
        tx.exec("UPDATE users SET status = $1, updated_at = now() WHERE id = $2::uuid RETURNING id",
                params);
    tx.commit();

    if (rows.empty()) {
      return Status(Error{"RESOURCE_NOT_FOUND", "用户不存在"});
    }
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "更新用户状态失败"));
  }
}

Status UserRepository::AddUsedStorage(std::string_view id, std::int64_t delta_bytes) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(delta_bytes);
    params.append(std::string(id));
    // GREATEST 兜底：统计字段是缓存，不能因为一次错误的负数调整变成非法值
    const pqxx::result rows = tx.exec(
        "UPDATE users SET used_storage_bytes = GREATEST(used_storage_bytes + $1, 0), "
        "updated_at = now() WHERE id = $2::uuid RETURNING id",
        params);
    tx.commit();
    if (rows.empty()) {
      return Status(Error{"RESOURCE_NOT_FOUND", "用户不存在"});
    }
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "更新已用空间失败"));
  }
}

Result<std::uint64_t> UserRepository::RecalculateUsedStorage(std::string_view id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(id));
    // 口径与迁移 0005 的回填一致：所有文件版本大小之和（含回收站与历史版本）
    const pqxx::result rows = tx.exec(
        "UPDATE users u SET used_storage_bytes = COALESCE(("
        "    SELECT SUM(v.size_bytes) FROM file_versions v "
        "    JOIN files f ON f.id = v.file_id WHERE f.owner_id = u.id), 0), "
        "updated_at = now() "
        "WHERE u.id = $1::uuid "
        "RETURNING used_storage_bytes",
        params);
    tx.commit();

    if (rows.empty()) {
      return Error{"RESOURCE_NOT_FOUND", "用户不存在"};
    }
    return rows[0]["used_storage_bytes"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "重算已用空间失败");
  }
}

namespace {

// LIKE 模式里的通配符必须转义，否则搜 "100%" 会退化成"匹配所有"
std::string EscapeLikePattern(std::string_view input) {
  std::string escaped;
  escaped.reserve(input.size());
  for (const char c : input) {
    if (c == '\\' || c == '%' || c == '_') {
      escaped += '\\';
    }
    escaped += c;
  }
  return escaped;
}

// 排序列白名单：SQL 文本里只允许出现这里的固定片段
std::string SafeUserSortColumn(std::string_view requested) {
  constexpr std::array<std::string_view, 5> kAllowed{"username", "email", "created_at",
                                                     "used_storage_bytes", "storage_quota_bytes"};
  for (const auto candidate : kAllowed) {
    if (requested == candidate) {
      return std::string(candidate);
    }
  }
  return "created_at";
}

std::string UserWhere(const UserRepository::UserQuery& query, pqxx::params& params) {
  std::string where = " WHERE 1=1";
  if (!query.search.empty()) {
    params.append("%" + EscapeLikePattern(query.search) + "%");
    where += " AND (username ILIKE $" + std::to_string(params.size()) + " OR email ILIKE $" +
             std::to_string(params.size()) + ")";
  }
  if (!query.status.empty()) {
    params.append(query.status);
    where += " AND status = $" + std::to_string(params.size());
  }
  return where;
}

}  // namespace

Result<std::vector<User>> UserRepository::List(const UserQuery& query, std::string_view sort_column,
                                               bool descending, int limit, int offset) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    const std::string where = UserWhere(query, params);
    params.append(limit);
    params.append(offset);
    const std::string sql =
        "SELECT " + std::string(kSelectColumns) + " FROM users" + where + " ORDER BY " +
        SafeUserSortColumn(sort_column) + (descending ? " DESC" : " ASC") + ", id ASC LIMIT $" +
        std::to_string(params.size() - 1) + " OFFSET $" + std::to_string(params.size());
    const pqxx::result rows = tx.exec(sql, params);
    tx.commit();
    std::vector<User> users;
    users.reserve(rows.size());
    for (const auto& row : rows) {
      users.push_back(UserFromRow(row));
    }
    return users;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询用户列表失败");
  }
}

Result<std::uint64_t> UserRepository::Count(const UserQuery& query) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    const std::string where = UserWhere(query, params);
    const pqxx::result rows =
        tx.exec("SELECT count(*)::bigint AS total FROM users" + where, params);
    tx.commit();
    if (rows.empty()) {
      return static_cast<std::uint64_t>(0);
    }
    return rows[0]["total"].as<std::uint64_t>();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计用户数失败");
  }
}

Status UserRepository::SetQuota(std::string_view id, std::uint64_t quota_bytes) {
  return SetQuotaForTesting(id, quota_bytes);
}

// 在线可见性（五阶段改造 ⑤）：默认可见，用户可在设置里隐身
Status UserRepository::SetPresenceVisible(std::string_view id, bool visible) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(visible);
    params.append(std::string(id));
    const pqxx::result rows = tx.exec(
        "UPDATE users SET presence_visible = $1, updated_at = now() "
        "WHERE id = $2::uuid RETURNING id",
        params);
    tx.commit();
    if (rows.empty()) {
      return Status(Error{"RESOURCE_NOT_FOUND", "用户不存在"});
    }
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "更新在线可见性失败"));
  }
}
Status UserRepository::SetQuotaForTesting(std::string_view id, std::uint64_t quota_bytes) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(quota_bytes);
    params.append(std::string(id));
    const pqxx::result rows = tx.exec(
        "UPDATE users SET storage_quota_bytes = $1, updated_at = now() "
        "WHERE id = $2::uuid RETURNING id",
        params);
    tx.commit();
    if (rows.empty()) {
      return Status(Error{"RESOURCE_NOT_FOUND", "用户不存在"});
    }
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "修改配额失败"));
  }
}

Status UserRepository::DeleteAllForTesting() {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }
  try {
    pqxx::work tx(lease->connection());
    tx.exec("DELETE FROM users");
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "清空 users 表失败"));
  }
}

}  // namespace lan_file_hub
