// 回收站应用服务（阶段八）。
//
// 语义（docs/phases/08-trash-and-restore.md、docs/recovery.md）：
//   * 软删除只改状态，不动磁盘对象；恢复因此是廉价的。
//   * 恢复必须幂等；原父目录不存在时恢复到用户根目录；同名冲突时拒绝并说明冲突对象。
//   * 永久删除在事务里删数据库记录、回收配额，然后把对象 Key 交给后台清理任务，
//     HTTP 请求不做长时间磁盘删除；对象删除失败由任务重试，**不会把数据库状态改成成功**。
//   * 删除、恢复、永久删除都写审计日志。
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "lan_file_hub/application/permission_checker.h"
#include "lan_file_hub/common/pagination.h"
#include "lan_file_hub/common/result.h"
#include "lan_file_hub/infrastructure/audit_repository.h"
#include "lan_file_hub/infrastructure/file_repository.h"
#include "lan_file_hub/infrastructure/folder_repository.h"
#include "lan_file_hub/infrastructure/permission_repository.h"
#include "lan_file_hub/infrastructure/trash_repository.h"

namespace lan_file_hub {

struct Actor;  // 定义在 folder_service.h

struct TrashServiceOptions {
  // 回收站保留时长，超过后由后台任务永久删除（默认 30 天）
  std::int64_t retention_seconds{30LL * 24 * 60 * 60};
};

// 恢复结果：告诉客户端实际恢复到了哪里（可能因为原目录不在而落到根目录）
struct RestoreOutcome {
  TrashItem item;
  std::string target_folder_id;
  bool moved_to_root{false};
  bool already_restored{false};
};

struct PurgeOutcome {
  std::string resource_id;
  std::string kind;
  std::uint64_t freed_bytes{0};
  std::uint64_t file_count{0};
  std::uint64_t folder_count{0};
  std::size_t queued_objects{0};
};

class TrashService {
 public:
  TrashService(TrashRepository& trash, FileRepository& files, FolderRepository& folders,
               AuditLogRepository& audit, CleanupTaskRepository& cleanup,
               PermissionRepository& permissions_repo, ShareLinkRepository& shares,
               const ResourcePermissionChecker& permissions, TrashServiceOptions options = {});

  // 回收站列表。管理员不传 owner 时看全部，普通用户只看自己的。
  [[nodiscard]] Result<TrashListing> List(const Actor& actor, const Pagination& pagination,
                                          std::string_view kind_filter);

  [[nodiscard]] Result<RestoreOutcome> Restore(const Actor& actor, std::string_view resource_id);

  // 永久删除：权限 + 状态检查 → 事务删记录 → 对象交给清理任务 → 审计
  [[nodiscard]] Result<PurgeOutcome> Purge(const Actor& actor, std::string_view resource_id);

  // 软删除一个文件（DELETE /files/{id} 使用）
  [[nodiscard]] Status SoftDeleteFile(const Actor& actor, std::string_view file_id);

  // 后台任务用：永久删除已过期的回收站条目
  [[nodiscard]] Result<std::size_t> PurgeExpired(int limit);

  [[nodiscard]] std::int64_t retention_seconds() const { return options_.retention_seconds; }

 private:
  [[nodiscard]] Status Audit(std::string_view actor_id, std::string_view action,
                             std::string_view resource_type, std::string_view resource_id,
                             const std::string& detail_json);

  TrashRepository& trash_;
  FileRepository& files_;
  FolderRepository& folders_;
  AuditLogRepository& audit_;
  CleanupTaskRepository& cleanup_;
  // 永久删除时清理悬空授权与分享链接（多态引用没有外键，必须显式删）
  PermissionRepository& permissions_repo_;
  ShareLinkRepository& shares_;
  const ResourcePermissionChecker& permissions_;
  TrashServiceOptions options_;
};

}  // namespace lan_file_hub
