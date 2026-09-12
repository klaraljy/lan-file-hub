// 管理员服务（阶段十三第 13～17 条）。
//
// 所有入口都先做管理员判定：普通用户一律 FORBIDDEN。
// 改变状态/配额的操作必须留审计记录；查询审计日志本身也要留痕
//（"谁看过审计"，这本身就是安全事件）。管理员访问用户资源留痕见 permissions.md §2.1。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "lan_file_hub/common/pagination.h"
#include "lan_file_hub/common/result.h"
#include "lan_file_hub/domain/user.h"
#include "lan_file_hub/infrastructure/audit_repository.h"
#include "lan_file_hub/infrastructure/file_repository.h"
#include "lan_file_hub/infrastructure/upload_session_repository.h"
#include "lan_file_hub/infrastructure/user_repository.h"
#include "lan_file_hub/storage/object_storage.h"

namespace lan_file_hub {

struct Actor;  // 定义在 folder_service.h

struct AdminUserPage {
  std::vector<User> users;
  std::uint64_t total{0};
};

// 系统存储统计：都是聚合数字，不含任何用户内容
struct SystemStorageStats {
  std::uint64_t user_count{0};
  std::uint64_t file_count{0};
  std::uint64_t version_count{0};
  std::uint64_t used_bytes{0};
  std::uint64_t free_bytes{0};
  std::uint64_t active_uploads{0};
};

struct AuditLogFilter {
  std::string actor_id;
  std::string action;
  std::string resource_type;
  std::string resource_id;
};

struct AuditLogPage {
  std::vector<AuditEntry> entries;
  std::uint64_t total{0};
};

class AdminService {
 public:
  AdminService(UserRepository& users, FileRepository& files, AuditLogRepository& audit,
               UploadSessionRepository& sessions, ObjectStorage& storage);

  [[nodiscard]] Result<AdminUserPage> ListUsers(const Actor& actor, std::string_view search,
                                                std::string_view status, std::string_view sort_by,
                                                bool descending, const Pagination& pagination);

  [[nodiscard]] Status SetUserStatus(const Actor& actor, std::string_view user_id,
                                     UserStatus status);

  [[nodiscard]] Status SetUserQuota(const Actor& actor, std::string_view user_id,
                                    std::uint64_t quota_bytes);

  [[nodiscard]] Result<SystemStorageStats> StorageStats(const Actor& actor);

  [[nodiscard]] Result<AuditLogPage> QueryAuditLogs(const Actor& actor,
                                                    const AuditLogFilter& filter,
                                                    const Pagination& pagination);

 private:
  [[nodiscard]] Status RequireAdmin(const Actor& actor) const;

  // 写审计记录；失败只记日志，不让审计失败改变业务结果（否则会把管理员锁在门外）
  void AuditAdminAction(const Actor& actor, std::string_view action, std::string_view resource_type,
                        std::string_view resource_id, const std::string& detail_json) const;

  UserRepository& users_;
  FileRepository& files_;
  AuditLogRepository& audit_;
  UploadSessionRepository& sessions_;
  ObjectStorage& storage_;
};

}  // namespace lan_file_hub
