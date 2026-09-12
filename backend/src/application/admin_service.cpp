#include "lan_file_hub/application/admin_service.h"

#include <string>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "lan_file_hub/application/folder_service.h"  // Actor
#include "lan_file_hub/common/uuid.h"

namespace lan_file_hub {
namespace {

// 管理员列表的排序白名单：列名是固定片段，用户输入只用于选择，不参与拼接
constexpr std::string_view kSortCreatedAt = "created_at";
constexpr std::string_view kSortUsername = "username";
constexpr std::string_view kSortEmail = "email";
constexpr std::string_view kSortUsed = "used_storage_bytes";
constexpr std::string_view kSortQuota = "storage_quota_bytes";

// 返回空串表示"不在白名单里"：调用方必须报 INVALID_ARGUMENT，不能静默回退
//（静默回退会让客户端以为排序生效了）。
std::string ResolveSortColumn(std::string_view sort_by) {
  if (sort_by.empty() || sort_by == "name" || sort_by == "username") {
    return std::string(kSortUsername);
  }
  if (sort_by == "email")
    return std::string(kSortEmail);
  if (sort_by == "created_at")
    return std::string(kSortCreatedAt);
  if (sort_by == "used_storage_bytes")
    return std::string(kSortUsed);
  if (sort_by == "storage_quota_bytes")
    return std::string(kSortQuota);
  return {};
}

std::string StatusToDb(UserStatus status) {
  return status == UserStatus::kActive ? "ACTIVE" : "DISABLED";
}

}  // namespace

AdminService::AdminService(UserRepository& users, FileRepository& files, AuditLogRepository& audit,
                           UploadSessionRepository& sessions, ObjectStorage& storage)
    : users_(users), files_(files), audit_(audit), sessions_(sessions), storage_(storage) {}

Status AdminService::RequireAdmin(const Actor& actor) const {
  if (actor.role != UserRole::kAdmin) {
    // 与"资源不存在"区分开：管理员接口的存在不是秘密，但必须明确拒绝
    return Status(Error{"FORBIDDEN", "需要管理员权限"});
  }
  return Status::Success();
}

void AdminService::AuditAdminAction(const Actor& actor, std::string_view action,
                                    std::string_view resource_type, std::string_view resource_id,
                                    const std::string& detail_json) const {
  AuditEntry entry;
  entry.id = GenerateUuidV4();
  entry.actor_id = actor.user_id;
  entry.action = std::string(action);
  entry.resource_type = std::string(resource_type);
  entry.resource_id = std::string(resource_id);
  entry.detail_json = detail_json;
  const auto recorded = audit_.Record(entry);
  if (!recorded.Ok()) {
    spdlog::error("管理员操作审计写入失败 | action={} | resource_id={} | error={}", action,
                  resource_id, recorded.GetError().detail);
  }
}

Result<AdminUserPage> AdminService::ListUsers(const Actor& actor, std::string_view search,
                                              std::string_view status, std::string_view sort_by,
                                              bool descending, const Pagination& pagination) {
  const auto allowed = RequireAdmin(actor);
  if (!allowed.Ok()) {
    return allowed.GetError();
  }

  UserRepository::UserQuery query;
  query.search = std::string(search);
  query.status = std::string(status);
  if (!query.status.empty() && query.status != "ACTIVE" && query.status != "DISABLED") {
    return Error{"INVALID_ARGUMENT", "status 只能是 ACTIVE 或 DISABLED"};
  }

  const std::string sort_column = ResolveSortColumn(sort_by);
  if (sort_column.empty()) {
    return Error{"INVALID_ARGUMENT",
                 "sort_by 只支持 name/username/email/created_at/used_storage_bytes/"
                 "storage_quota_bytes"};
  }

  auto users =
      users_.List(query, sort_column, descending, pagination.page_size, pagination.Offset());
  if (!users) {
    return users.GetError();
  }
  auto total = users_.Count(query);
  if (!total) {
    return total.GetError();
  }

  AdminUserPage page;
  page.users = std::move(users.Value());
  page.total = total.Value();
  return page;
}

Status AdminService::SetUserStatus(const Actor& actor, std::string_view user_id,
                                   UserStatus status) {
  const auto allowed = RequireAdmin(actor);
  if (!allowed.Ok()) {
    return Status(allowed.GetError());
  }
  if (user_id == actor.user_id) {
    // 防止管理员把自己锁在门外（禁用后连登录都进不来）
    return Status(Error{"INVALID_ARGUMENT", "不能修改自己的账号状态"});
  }

  auto target = users_.FindById(user_id);
  if (!target) {
    return Status(target.GetError());
  }
  if (!target.Value().has_value()) {
    return Status(Error{"RESOURCE_NOT_FOUND", "用户不存在"});
  }
  if (target.Value()->status == status) {
    return Status::Success();  // 幂等：重复禁用/启用不报错
  }

  const auto updated = users_.UpdateStatus(user_id, status);
  if (!updated.Ok()) {
    return updated;
  }
  AuditAdminAction(
      actor, status == UserStatus::kActive ? "admin_enable_user" : "admin_disable_user", "user",
      user_id,
      nlohmann::json{{"username", target.Value()->username}, {"status", StatusToDb(status)}}
          .dump());
  spdlog::warn("管理员修改用户状态 | actor={} | target={} | status={}", actor.user_id, user_id,
               StatusToDb(status));
  return Status::Success();
}

Status AdminService::SetUserQuota(const Actor& actor, std::string_view user_id,
                                  std::uint64_t quota_bytes) {
  const auto allowed = RequireAdmin(actor);
  if (!allowed.Ok()) {
    return Status(allowed.GetError());
  }
  if (quota_bytes == 0) {
    return Status(Error{"INVALID_ARGUMENT", "配额必须大于 0"});
  }

  auto target = users_.FindById(user_id);
  if (!target) {
    return Status(target.GetError());
  }
  if (!target.Value().has_value()) {
    return Status(Error{"RESOURCE_NOT_FOUND", "用户不存在"});
  }

  const auto updated = users_.SetQuota(user_id, quota_bytes);
  if (!updated.Ok()) {
    return updated;
  }
  AuditAdminAction(actor, "admin_set_quota", "user", user_id,
                   nlohmann::json{{"username", target.Value()->username},
                                  {"old_quota_bytes", target.Value()->storage_quota_bytes},
                                  {"new_quota_bytes", quota_bytes}}
                       .dump());
  spdlog::warn("管理员修改用户配额 | actor={} | target={} | quota={}", actor.user_id, user_id,
               quota_bytes);
  return Status::Success();
}

Result<SystemStorageStats> AdminService::StorageStats(const Actor& actor) {
  const auto allowed = RequireAdmin(actor);
  if (!allowed.Ok()) {
    return allowed.GetError();
  }

  SystemStorageStats stats;
  auto users = users_.Count(UserRepository::UserQuery{});
  if (!users) {
    return users.GetError();
  }
  stats.user_count = users.Value();

  auto files = files_.CountAllFiles();
  if (!files) {
    return files.GetError();
  }
  stats.file_count = files.Value();

  auto versions = files_.CountAllVersions();
  if (!versions) {
    return versions.GetError();
  }
  stats.version_count = versions.Value();

  auto used = files_.SumAllVersionBytes();
  if (!used) {
    return used.GetError();
  }
  stats.used_bytes = used.Value();

  auto free_bytes = storage_.AvailableBytes();
  if (!free_bytes) {
    return free_bytes.GetError();
  }
  stats.free_bytes = free_bytes.Value();

  auto active = sessions_.CountInProgressGlobal();
  if (!active) {
    return active.GetError();
  }
  stats.active_uploads = active.Value();
  return stats;
}

Result<AuditLogPage> AdminService::QueryAuditLogs(const Actor& actor, const AuditLogFilter& filter,
                                                  const Pagination& pagination) {
  const auto allowed = RequireAdmin(actor);
  if (!allowed.Ok()) {
    return allowed.GetError();
  }

  AuditLogRepository::AuditQuery query;
  query.actor_id = filter.actor_id;
  query.action = filter.action;
  query.resource_type = filter.resource_type;
  query.resource_id = filter.resource_id;

  auto entries = audit_.List(query, pagination.page_size, pagination.Offset());
  if (!entries) {
    return entries.GetError();
  }
  auto total = audit_.Count(query);
  if (!total) {
    return total.GetError();
  }

  // 查审计日志本身也要留痕：谁在什么时候翻了日志
  AuditAdminAction(actor, "admin_query_audit_logs", "audit_log", actor.user_id,
                   nlohmann::json{{"page", pagination.page},
                                  {"page_size", pagination.page_size},
                                  {"action_filter", filter.action}}
                       .dump());

  AuditLogPage page;
  page.entries = std::move(entries.Value());
  page.total = total.Value();
  return page;
}

}  // namespace lan_file_hub
