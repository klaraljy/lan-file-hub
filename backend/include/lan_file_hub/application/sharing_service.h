// 资源权限管理与分享链接（阶段九）。
//
// 共同约定：
//   * 权限一律由服务端判定：改权限、建/撤销分享链接都要求 OWNER，管理员放行但必须写审计。
//   * 分享 Token 明文只在创建响应里返回一次，数据库只存 SHA-256。
//   * 公开接口（无需登录）同样要在服务端校验过期、撤销与下载次数，且只能访问它绑定的那一个资源。
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lan_file_hub/application/permission_checker.h"
#include "lan_file_hub/common/result.h"
#include "lan_file_hub/domain/file.h"
#include "lan_file_hub/infrastructure/audit_repository.h"
#include "lan_file_hub/infrastructure/file_repository.h"
#include "lan_file_hub/infrastructure/folder_repository.h"
#include "lan_file_hub/infrastructure/jwt_service.h"
#include "lan_file_hub/infrastructure/password_hasher.h"
#include "lan_file_hub/infrastructure/permission_repository.h"
#include "lan_file_hub/infrastructure/user_repository.h"
#include "lan_file_hub/storage/object_storage.h"

namespace lan_file_hub {

struct Actor;  // 定义在 folder_service.h

// 资源解析结果：请求只带 resource_id，服务端自己判断是文件还是文件夹
struct ResolvedResource {
  std::string type;  // "file" | "folder"
  std::string id;
  std::string owner_id;
  std::string name;
  std::uint64_t size_bytes{0};
  std::string mime_type;
  std::string folder_id;  // 文件所属目录
  bool active{true};
};

struct ShareCreateRequest {
  std::string permission{"READ"};                  // READ | WRITE
  std::optional<std::int64_t> expires_in_seconds;  // 空表示不自动过期
  std::optional<std::int32_t> max_downloads;       // 空表示不限次数
  // 分享密码（2026-10-03）：空 = 不需要密码（默认）。
  // 服务端用 Argon2id 哈希后入库，明文不落库、不进日志、不回显。
  std::optional<std::string> password;
};

struct ShareView {
  ShareLink link;
  std::string plaintext_token;  // 只在创建时填充
  bool download_allowed{true};
};

// 公开访问的资源视图：**不含**任何内部标识（owner、bucket、storage_key）
struct PublicShareView {
  std::string name;
  std::string resource_type;
  std::uint64_t size_bytes{0};
  std::string mime_type;
  std::string permission;
  std::optional<std::string> expires_at;
  std::int32_t download_count{0};
  std::optional<std::int32_t> max_downloads;
  bool can_download{true};
  std::string download_blocked_reason;
  // 这条分享要不要密码（2026-10-03）。为 true 时上面的名字/大小/类型**一律为空**
  // ——没输密码之前不该看到任何元数据（否则密码只挡下载、挡不住"文件叫什么"）。
  bool requires_password{false};
};

// 用密码解锁之后拿到的东西：资源信息 + 一张短票据（下载时带上它）
struct PublicUnlock {
  PublicShareView share;
  std::string download_ticket;  // 无密码分享时为空（本来就能直接下）
  std::int64_t ticket_expires_in_seconds{0};
};

class SharingService {
 public:
  SharingService(PermissionRepository& permissions, ShareLinkRepository& shares,
                 FileRepository& files, FolderRepository& folders, UserRepository& users,
                 AuditLogRepository& audit, ObjectStorage& storage,
                 const ResourcePermissionChecker& checker, const PasswordHasher& hasher,
                 const JwtService& jwt);

  // ---- 权限管理 ----

  [[nodiscard]] Result<ResolvedResource> Resolve(std::string_view resource_id);

  [[nodiscard]] Result<std::vector<PermissionGrant>> ListGrants(const Actor& actor,
                                                                std::string_view resource_id);

  [[nodiscard]] Result<PermissionGrant> Grant(const Actor& actor, std::string_view resource_id,
                                              std::string_view subject_id,
                                              std::string_view permission);

  [[nodiscard]] Status RevokeGrant(const Actor& actor, std::string_view resource_id,
                                   std::string_view subject_id);

  // 当前用户对该资源的有效级别（前端据此决定展示哪些按钮；后端仍然独立校验）
  [[nodiscard]] Result<std::string> EffectiveLevel(const Actor& actor,
                                                   std::string_view resource_id);

  // 「分享给我的」：当前用户被直接授权、且仍然存在的资源
  [[nodiscard]] Result<std::vector<PermissionRepository::SharedResource>> SharedWith(
      const Actor& actor, int limit);

  // ---- 分享链接 ----

  [[nodiscard]] Result<ShareView> CreateShare(const Actor& actor, std::string_view resource_id,
                                              const ShareCreateRequest& request);

  [[nodiscard]] Result<std::vector<ShareLink>> ListShares(const Actor& actor,
                                                          std::string_view resource_id);

  [[nodiscard]] Status RevokeShare(const Actor& actor, std::string_view share_id);

  // ---- 公开访问（无登录态）----

  [[nodiscard]] Result<PublicShareView> DescribePublic(std::string_view token);

  // 用密码解锁一条分享（无密码的分享也走这里：等价于"直接放行"）。
  // 密码错/分享不存在/已过期/次数用完，一律返回同一类错误，不泄露具体原因。
  [[nodiscard]] Result<PublicUnlock> UnlockPublic(std::string_view token,
                                                  std::string_view password);

  struct PublicDownload {
    ResolvedResource resource;
    std::string absolute_path;
    File file;  // 供 HTTP 层读 size/mime/name
  };

  // 校验通过后**原子地**消费一次下载次数，再解析物理路径。
  // ticket：需要密码的分享必须带上解锁时拿到的那张票据（⛔ 否则拿 URL 就能绕过密码）。
  [[nodiscard]] Result<PublicDownload> PreparePublicDownload(std::string_view token,
                                                             std::string_view ticket);

  // 管理员访问他人资源时补审计（读取类操作调用）
  void AuditAdminAccess(const Actor& actor, const ResolvedResource& resource,
                        std::string_view action);

 private:
  [[nodiscard]] Status CanManage(const Actor& actor, const ResolvedResource& resource);

  // 组装公开视图（不含任何内部标识）；密码门在 UnlockPublic 里，不在这里
  [[nodiscard]] Result<PublicShareView> BuildPublicView(const ShareLink& link);

  PermissionRepository& permissions_;
  ShareLinkRepository& shares_;
  FileRepository& files_;
  FolderRepository& folders_;
  UserRepository& users_;
  AuditLogRepository& audit_;
  ObjectStorage& storage_;
  const ResourcePermissionChecker& checker_;
  const PasswordHasher& hasher_;
  const JwtService& jwt_;
};

}  // namespace lan_file_hub
