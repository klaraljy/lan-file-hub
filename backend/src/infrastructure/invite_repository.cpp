#include "lan_file_hub/infrastructure/invite_repository.h"

#include <string>

#include <pqxx/pqxx>

#include "infrastructure/database_error.h"
#include "lan_file_hub/infrastructure/permission_repository.h"  // GenerateShareToken / HashShareToken

namespace lan_file_hub {
namespace {

// 统一的时间输出：UTC ISO8601，与 docs/api.md 的「统一时间格式」一致；
// 判定用的 epoch 秒一并取出，省得在 C++ 里解析字符串。
constexpr char kSelectColumns[] =
    "id::text AS id, note, created_by::text AS created_by, "
    "to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at, "
    "CASE WHEN expires_at IS NULL THEN NULL "
    "     ELSE to_char(expires_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') END "
    "  AS expires_at, "
    "CASE WHEN expires_at IS NULL THEN NULL ELSE extract(epoch FROM expires_at)::bigint END "
    "  AS expires_at_unix, "
    "max_uses, used_count, "
    "CASE WHEN revoked_at IS NULL THEN NULL "
    "     ELSE to_char(revoked_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') END "
    "  AS revoked_at";

template <typename RowType>
Invite InviteFromRow(const RowType& row) {
  Invite invite;
  invite.id = row["id"].template as<std::string>();
  invite.note = row["note"].template as<std::string>();
  invite.created_by = row["created_by"].template as<std::string>();
  invite.created_at = row["created_at"].template as<std::string>();
  if (!row["expires_at"].is_null()) {
    invite.expires_at = row["expires_at"].template as<std::string>();
  }
  if (!row["expires_at_unix"].is_null()) {
    invite.expires_at_unix = row["expires_at_unix"].template as<std::int64_t>();
  }
  invite.max_uses = row["max_uses"].template as<std::int32_t>();
  invite.used_count = row["used_count"].template as<std::int32_t>();
  if (!row["revoked_at"].is_null()) {
    invite.revoked_at = row["revoked_at"].template as<std::string>();
  }
  return invite;
}

}  // namespace

Result<Invite> InviteRepository::Create(std::string_view token_hash, std::string_view note,
                                        std::string_view created_by,
                                        std::optional<std::int64_t> expires_at_unix,
                                        std::int32_t max_uses) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(token_hash));
    params.append(std::string(note));
    params.append(std::string(created_by));
    params.append(expires_at_unix.value_or(0));
    params.append(max_uses);

    // 0 表示不过期 → 存 NULL（用 SQL 的 now() 比较，避免客户端时钟偏差）
    const pqxx::result rows = tx.exec(
        "INSERT INTO invites (token_hash, note, created_by, expires_at, max_uses) VALUES "
        "($1, $2, $3::uuid, CASE WHEN $4::bigint = 0 THEN NULL ELSE to_timestamp($4::bigint) END, "
        "$5) RETURNING " +
            std::string(kSelectColumns),
        params);
    tx.commit();

    if (rows.empty()) {
      return Error{"INTERNAL_ERROR", "创建邀请未返回记录"};
    }
    return InviteFromRow(rows[0]);
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "创建邀请失败");
  }
}

Result<std::optional<Invite>> InviteRepository::FindByTokenHash(std::string_view token_hash) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(token_hash));
    const pqxx::result rows =
        tx.exec("SELECT " + std::string(kSelectColumns) + " FROM invites WHERE token_hash = $1",
                params);
    tx.commit();

    if (rows.empty()) {
      return std::optional<Invite>{};
    }
    return std::optional<Invite>{InviteFromRow(rows[0])};
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询邀请失败");
  }
}

Result<std::vector<Invite>> InviteRepository::List(int limit, int offset) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(limit);
    params.append(offset);
    const pqxx::result rows = tx.exec("SELECT " + std::string(kSelectColumns) +
                                          " FROM invites ORDER BY created_at DESC LIMIT $1 OFFSET $2",
                                      params);
    tx.commit();

    std::vector<Invite> invites;
    invites.reserve(rows.size());
    for (const auto& row : rows) {
      invites.push_back(InviteFromRow(row));
    }
    return invites;
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "查询邀请列表失败");
  }
}

Result<std::uint64_t> InviteRepository::Count() {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    const pqxx::result rows = tx.exec("SELECT count(*)::bigint AS total FROM invites");
    tx.commit();
    return static_cast<std::uint64_t>(rows[0]["total"].as<std::int64_t>());
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "统计邀请数量失败");
  }
}

Result<bool> InviteRepository::Consume(std::string_view token_hash) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"};
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(token_hash));
    // ⛔ 所有条件都写在 WHERE 里，靠这一条语句完成"检查 + 消耗"，避免先查后改的竞态
    const pqxx::result rows = tx.exec(
        "UPDATE invites SET used_count = used_count + 1, updated_at = now() "
        "WHERE token_hash = $1 AND revoked_at IS NULL AND used_count < max_uses "
        "  AND (expires_at IS NULL OR expires_at > now()) RETURNING id",
        params);
    tx.commit();
    return !rows.empty();
  } catch (const std::exception& ex) {
    return DatabaseError(ex, "消耗邀请失败");
  }
}

Status InviteRepository::Release(std::string_view token_hash) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(token_hash));
    tx.exec(
        "UPDATE invites SET used_count = GREATEST(used_count - 1, 0), updated_at = now() "
        "WHERE token_hash = $1",
        params);
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "归还邀请次数失败"));
  }
}

Status InviteRepository::Revoke(std::string_view invite_id) {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }

  try {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(std::string(invite_id));
    // 幂等：已撤销或不存在都不算错误
    tx.exec("UPDATE invites SET revoked_at = now(), updated_at = now() "
            "WHERE id = $1::uuid AND revoked_at IS NULL",
            params);
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    // 非 UUID 的 id 会在这里报错，归为"参数非法"也由这里统一转成错误
    return Status(DatabaseError(ex, "撤销邀请失败"));
  }
}

Status InviteRepository::DeleteAllForTesting() {
  auto lease = pool_.Acquire();
  if (!lease) {
    return Status(Error{"STORAGE_UNAVAILABLE", "数据库繁忙，请稍后重试"});
  }

  try {
    pqxx::work tx(lease->connection());
    tx.exec("DELETE FROM invites");
    tx.commit();
    return Status::Success();
  } catch (const std::exception& ex) {
    return Status(DatabaseError(ex, "清空 invites 失败"));
  }
}

Result<std::string> GenerateInviteToken() {
  return GenerateShareToken();
}

std::string HashInviteToken(std::string_view token) {
  return HashShareToken(token);
}

}  // namespace lan_file_hub
