// 进程内共享的服务集合。启动时装配一次，之后只读。
// 控制器与过滤器通过 GetAppContext() 取用，避免到处传参或使用全局可变状态。
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "lan_file_hub/application/admin_service.h"
#include "lan_file_hub/application/app_release.h"
#include "lan_file_hub/application/auth_service.h"
#include "lan_file_hub/application/cleanup_service.h"
#include "lan_file_hub/application/file_scanner.h"
#include "lan_file_hub/application/file_service.h"
#include "lan_file_hub/application/folder_service.h"
#include "lan_file_hub/application/invite_service.h"
#include "lan_file_hub/application/permission_checker.h"
#include "lan_file_hub/application/presence_registry.h"
#include "lan_file_hub/application/sharing_service.h"
#include "lan_file_hub/application/trash_service.h"
#include "lan_file_hub/application/upload_service.h"
#include "lan_file_hub/common/byte_rate_limiter.h"
#include "lan_file_hub/common/rate_limiter.h"
#include "lan_file_hub/infrastructure/audit_repository.h"
#include "lan_file_hub/infrastructure/database.h"
#include "lan_file_hub/infrastructure/file_repository.h"
#include "lan_file_hub/infrastructure/folder_repository.h"
#include "lan_file_hub/infrastructure/invite_repository.h"
#include "lan_file_hub/infrastructure/jwt_service.h"
#include "lan_file_hub/infrastructure/password_hasher.h"
#include "lan_file_hub/infrastructure/permission_repository.h"
#include "lan_file_hub/infrastructure/trash_repository.h"
#include "lan_file_hub/infrastructure/upload_session_repository.h"
#include "lan_file_hub/infrastructure/user_repository.h"
#include "lan_file_hub/storage/object_storage.h"
namespace lan_file_hub {

struct AppContext {
  std::shared_ptr<ConnectionPool> pool;

  // 仓储
  std::shared_ptr<UserRepository> users;
  std::shared_ptr<FolderRepository> folders;
  std::shared_ptr<FileRepository> files;
  std::shared_ptr<UploadSessionRepository> uploads;
  std::shared_ptr<TrashRepository> trash;
  std::shared_ptr<AuditLogRepository> audit;
  std::shared_ptr<CleanupTaskRepository> cleanup_tasks;
  std::shared_ptr<PermissionRepository> permissions_repo;
  std::shared_ptr<ShareLinkRepository> shares;
  // 邀请（用户 2026-10-02：邀请制注册）
  std::shared_ptr<InviteRepository> invites_repo;

  // 基础设施服务
  std::shared_ptr<PasswordHasher> password_hasher;
  std::shared_ptr<JwtService> jwt;
  std::shared_ptr<ObjectStorage> storage;

  // 权限检查（阶段九起基于 permissions 表 + 文件夹继承）
  std::shared_ptr<ResourcePermissionChecker> permissions;

  // 应用服务
  std::shared_ptr<AuthService> auth;
  std::shared_ptr<FolderService> folders_service;
  std::shared_ptr<FileService> files_service;
  std::shared_ptr<UploadService> uploads_service;
  std::shared_ptr<TrashService> trash_service;
  std::shared_ptr<SharingService> sharing_service;
  // 邀请业务规则（建/查/撤销/消耗/归还）
  std::shared_ptr<InviteService> invites;
  // 是否允许自助注册（来自 ALLOW_SELF_REGISTRATION；false 时注册必须带有效邀请）
  bool allow_self_registration{true};
  // 后台清理（过期回收站 + 对象异步删除），进程退出时有序停止
  std::shared_ptr<CleanupService> cleanup_service;

  // 限流器（阶段十二）：进程内滑动窗口。
  // 登录失败按 (IP + 账号) 计数，另有一层按 IP 的宽松上限挡住"换着用户名撞库"；
  // 上传按用户计数，避免单用户刷爆会话表。
  std::shared_ptr<RateLimiter> login_limiter;
  std::shared_ptr<RateLimiter> login_ip_limiter;
  std::shared_ptr<RateLimiter> upload_limiter;
  // 分享密码解锁限流（2026-10-03）：按 IP + 分享 Token 计数，挡住暴力猜密码
  std::shared_ptr<RateLimiter> share_unlock_limiter;
  // 字节级限速（阶段十二第 4 条）：按用户（公开下载按分享链接）限制下载速率；
  // 未配置速率时 enabled() 为 false，下载继续走零拷贝路径
  std::shared_ptr<ByteRateLimiter> download_limiter;

  // 内容安全扫描（阶段十二第 17 条）：本期固定是 NoopFileScanner
  std::shared_ptr<FileScanner> scanner;

  // 管理员服务（阶段十三）：用户列表、启用/禁用、配额、系统存储统计、审计日志查询
  std::shared_ptr<AdminService> admin_service;

  // 在线状态（五阶段改造 ⑤：广场 / 在线用户）——进程内存，不落库、重启即清空
  std::shared_ptr<PresenceRegistry> presence;

  // 配套手机应用的安装包信息（用户 2026-10-03）：每个请求现看磁盘，重新构建后不用重启
  std::shared_ptr<AppRelease> app_release;

  // 文件对象根目录（启动时从配置读一次）：只给管理端「文件存在哪」展示用。
  // ⛔ 这里只有路径，没有任何凭据；只对 ADMIN 返回（docs/api.md 管理员接口）。
  std::string storage_root_path;

  // 需要脱敏的值（数据库口令、Redis 口令、JWT 密钥），日志与错误信息输出前必须过滤
  std::vector<std::string> secrets;
};

void SetAppContext(std::shared_ptr<AppContext> context);
[[nodiscard]] std::shared_ptr<AppContext> GetAppContext();

// 用 secrets 过滤敏感字符串（找不到 context 时原样返回）。
[[nodiscard]] std::string RedactWithContext(std::string text);

}  // namespace lan_file_hub
