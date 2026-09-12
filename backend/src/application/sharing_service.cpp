#include "lan_file_hub/application/sharing_service.h"

#include <string>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "lan_file_hub/application/folder_service.h"  // Actor
#include "lan_file_hub/common/uuid.h"

namespace lan_file_hub {
namespace {

Error NotFound() {
  return Error{"RESOURCE_NOT_FOUND", "资源不存在"};
}
Error ShareNotFound() {
  return Error{"RESOURCE_NOT_FOUND", "分享链接无效或已失效"};
}

// 只允许这两级出现在分享链接上（docs/permissions.md §1）
bool ValidSharePermission(const std::string& value) {
  return value == "READ" || value == "WRITE";
}

bool ValidGrantPermission(const std::string& value) {
  return value == "READ" || value == "WRITE" || value == "OWNER";
}

std::string JsonCompact(const nlohmann::json& value) {
  return value.dump();
}

}  // namespace

SharingService::SharingService(PermissionRepository& permissions, ShareLinkRepository& shares,
                               FileRepository& files, FolderRepository& folders,
                               UserRepository& users, AuditLogRepository& audit,
                               ObjectStorage& storage, const ResourcePermissionChecker& checker)
    : permissions_(permissions),
      shares_(shares),
      files_(files),
      folders_(folders),
      users_(users),
      audit_(audit),
      storage_(storage),
      checker_(checker) {}

Result<ResolvedResource> SharingService::Resolve(std::string_view resource_id) {
  if (resource_id.empty()) {
    return Error{"INVALID_ARGUMENT", "缺少资源标识"};
  }

  auto file = files_.FindById(resource_id);
  if (!file) {
    return file.GetError();
  }
  if (file.Value().has_value()) {
    const File& record = *file.Value();
    ResolvedResource resolved;
    resolved.type = "file";
    resolved.id = record.id;
    resolved.owner_id = record.owner_id;
    resolved.name = record.original_name;
    resolved.size_bytes = record.size_bytes;
    resolved.mime_type = record.mime_type;
    resolved.folder_id = record.folder_id;
    resolved.active = record.IsActive();
    return resolved;
  }

  auto folder = folders_.FindById(resource_id);
  if (!folder) {
    return folder.GetError();
  }
  if (folder.Value().has_value()) {
    const Folder& record = *folder.Value();
    ResolvedResource resolved;
    resolved.type = "folder";
    resolved.id = record.id;
    resolved.owner_id = record.owner_id;
    resolved.name = record.IsRoot() ? std::string("我的文件") : record.name;
    resolved.folder_id = record.parent_id.value_or("");
    resolved.active = record.status == FolderStatus::kActive;
    return resolved;
  }

  return NotFound();
}

Status SharingService::CanManage(const Actor& actor, const ResolvedResource& resource) {
  // kManage 需要 OWNER：权限与分享都是资源级的敏感操作
  auto allowed = checker_.Can(
      actor.user_id, actor.role,
      ResourceDescriptor{resource.owner_id, resource.type, resource.id, resource.folder_id},
      ResourceAction::kManage);
  if (!allowed) {
    return Status(allowed.GetError());
  }
  if (!allowed.Value()) {
    // 无权限与不存在返回同样的错误，避免探测他人资源
    return Status(NotFound());
  }
  return Status::Success();
}

void SharingService::AuditAdminAccess(const Actor& actor, const ResolvedResource& resource,
                                      std::string_view action) {
  if (actor.role != UserRole::kAdmin || actor.user_id == resource.owner_id) {
    return;
  }
  // 管理员访问用户资源必须留痕（docs/permissions.md §2.1）
  AuditEntry entry;
  entry.id = GenerateUuidV4();
  entry.actor_id = actor.user_id;
  entry.action = std::string("admin_") + std::string(action);
  entry.resource_type = resource.type;
  entry.resource_id = resource.id;
  entry.detail_json = JsonCompact({{"owner_id", resource.owner_id}, {"name", resource.name}});
  auto status = audit_.Record(entry);
  if (!status.Ok()) {
    spdlog::error("管理员访问审计写入失败 | action={} | resource_id={} | error={}", action,
                  resource.id, status.GetError().detail);
  }
}

Result<std::vector<PermissionGrant>> SharingService::ListGrants(const Actor& actor,
                                                                std::string_view resource_id) {
  auto resource = Resolve(resource_id);
  if (!resource) {
    return resource.GetError();
  }
  auto allowed = CanManage(actor, resource.Value());
  if (!allowed.Ok()) {
    return allowed.GetError();
  }
  return permissions_.ListForResource(resource.Value().type, resource.Value().id);
}

Result<PermissionGrant> SharingService::Grant(const Actor& actor, std::string_view resource_id,
                                              std::string_view subject_id,
                                              std::string_view permission) {
  auto resource = Resolve(resource_id);
  if (!resource) {
    return resource.GetError();
  }
  auto allowed = CanManage(actor, resource.Value());
  if (!allowed.Ok()) {
    return allowed.GetError();
  }
  if (!ValidGrantPermission(std::string(permission))) {
    return Error{"INVALID_ARGUMENT", "permission 只能是 READ、WRITE 或 OWNER"};
  }

  // 被授权人必须存在且可用：给不存在的用户授权等于制造悬空记录
  auto subject = users_.FindById(subject_id);
  if (!subject) {
    return subject.GetError();
  }
  if (!subject.Value().has_value()) {
    return Error{"RESOURCE_NOT_FOUND", "被授权的用户不存在"};
  }
  if (!subject.Value()->IsActive()) {
    return Error{"INVALID_ARGUMENT", "不能给已禁用的用户授权"};
  }

  auto grant = permissions_.Upsert(resource.Value().type, resource.Value().id, subject_id,
                                   permission, actor.user_id);
  if (!grant) {
    return grant.GetError();
  }

  AuditEntry entry;
  entry.id = GenerateUuidV4();
  entry.actor_id = actor.user_id;
  entry.action = "grant_permission";
  entry.resource_type = resource.Value().type;
  entry.resource_id = resource.Value().id;
  entry.detail_json =
      JsonCompact({{"subject_id", std::string(subject_id)}, {"permission", permission}});
  (void)audit_.Record(entry);

  spdlog::info("授予权限 | actor={} | resource={}:{} | subject={} | level={}", actor.user_id,
               resource.Value().type, resource.Value().id, subject_id, permission);
  return grant.Value();
}

Status SharingService::RevokeGrant(const Actor& actor, std::string_view resource_id,
                                   std::string_view subject_id) {
  auto resource = Resolve(resource_id);
  if (!resource) {
    return Status(resource.GetError());
  }
  auto allowed = CanManage(actor, resource.Value());
  if (!allowed.Ok()) {
    return allowed;
  }

  auto status = permissions_.Remove(resource.Value().type, resource.Value().id, subject_id);
  if (!status.Ok()) {
    return status;
  }

  AuditEntry entry;
  entry.id = GenerateUuidV4();
  entry.actor_id = actor.user_id;
  entry.action = "revoke_permission";
  entry.resource_type = resource.Value().type;
  entry.resource_id = resource.Value().id;
  entry.detail_json = JsonCompact({{"subject_id", std::string(subject_id)}});
  (void)audit_.Record(entry);

  spdlog::info("撤销权限 | actor={} | resource={}:{} | subject={}", actor.user_id,
               resource.Value().type, resource.Value().id, subject_id);
  return Status::Success();
}

Result<std::string> SharingService::EffectiveLevel(const Actor& actor,
                                                   std::string_view resource_id) {
  auto resource = Resolve(resource_id);
  if (!resource) {
    return resource.GetError();
  }
  const ResolvedResource& resolved = resource.Value();
  if (actor.user_id == resolved.owner_id) {
    return std::string("OWNER");
  }
  if (actor.role == UserRole::kAdmin) {
    return std::string("ADMIN");
  }
  auto level = checker_.EffectiveLevel(
      actor.user_id,
      ResourceDescriptor{resolved.owner_id, resolved.type, resolved.id, resolved.folder_id});
  if (!level) {
    return level.GetError();
  }
  return ToDbString(level.Value());
}

Result<std::vector<PermissionRepository::SharedResource>> SharingService::SharedWith(
    const Actor& actor, int limit) {
  if (actor.user_id.empty()) {
    return Error{"UNAUTHENTICATED", "缺少用户标识"};
  }
  // 只按「直接授权」查：继承来的权限通过目录访问即可，放进这个列表反而让人困惑
  return permissions_.ListSharedWith(actor.user_id, limit);
}

Result<ShareView> SharingService::CreateShare(const Actor& actor, std::string_view resource_id,
                                              const ShareCreateRequest& request) {
  auto resource = Resolve(resource_id);
  if (!resource) {
    return resource.GetError();
  }
  auto allowed = CanManage(actor, resource.Value());
  if (!allowed.Ok()) {
    return allowed.GetError();
  }
  if (!ValidSharePermission(request.permission)) {
    return Error{"INVALID_ARGUMENT", "分享权限只能是 READ 或 WRITE"};
  }
  if (request.max_downloads.has_value() && *request.max_downloads <= 0) {
    return Error{"INVALID_ARGUMENT", "max_downloads 必须大于 0"};
  }
  if (request.expires_in_seconds.has_value()) {
    if (*request.expires_in_seconds <= 0) {
      return Error{"INVALID_ARGUMENT", "expires_in_seconds 必须大于 0"};
    }
    // 上限一年，避免「永不过期」被伪装成很长的过期时间
    if (*request.expires_in_seconds > 365LL * 24 * 60 * 60) {
      return Error{"INVALID_ARGUMENT", "过期时间最长 365 天"};
    }
  }

  auto token = GenerateShareToken();
  if (!token) {
    return token.GetError();
  }

  ShareLink link;
  link.id = GenerateUuidV4();
  link.token_hash = HashShareToken(token.Value());
  link.resource_type = resource.Value().type;
  link.resource_id = resource.Value().id;
  link.permission = request.permission;
  link.max_downloads = request.max_downloads;
  link.created_by = actor.user_id;
  link.resource_name = resource.Value().name;
  // 过期时间交给数据库按 now() 计算，不受应用进程时钟影响
  link.expires_in_seconds = request.expires_in_seconds;

  auto created = shares_.Create(link);
  if (!created) {
    return created.GetError();
  }

  AuditEntry entry;
  entry.id = GenerateUuidV4();
  entry.actor_id = actor.user_id;
  entry.action = "create_share";
  entry.resource_type = resource.Value().type;
  entry.resource_id = resource.Value().id;
  entry.detail_json = JsonCompact({{"share_id", created.Value().id},
                                   {"permission", request.permission},
                                   {"max_downloads", request.max_downloads.value_or(0)}});
  (void)audit_.Record(entry);

  ShareView view;
  view.link = created.Value();
  // 明文 Token 只在这里出现一次，之后服务端再也拿不到
  view.plaintext_token = token.Value();
  view.download_allowed = resource.Value().type == "file";
  spdlog::info("创建分享链接 | actor={} | resource={}:{} | permission={} | share_id={}",
               actor.user_id, resource.Value().type, resource.Value().id, request.permission,
               created.Value().id);
  return view;
}

Result<std::vector<ShareLink>> SharingService::ListShares(const Actor& actor,
                                                          std::string_view resource_id) {
  auto resource = Resolve(resource_id);
  if (!resource) {
    return resource.GetError();
  }
  auto allowed = CanManage(actor, resource.Value());
  if (!allowed.Ok()) {
    return allowed.GetError();
  }
  return shares_.ListForResource(resource.Value().type, resource.Value().id);
}

Status SharingService::RevokeShare(const Actor& actor, std::string_view share_id) {
  auto found = shares_.FindById(share_id);
  if (!found) {
    return Status(found.GetError());
  }
  if (!found.Value().has_value()) {
    return Status(ShareNotFound());
  }
  const ShareLink link = *found.Value();

  auto resource = Resolve(link.resource_id);
  if (!resource) {
    return Status(resource.GetError());
  }
  auto allowed = CanManage(actor, resource.Value());
  if (!allowed.Ok()) {
    return allowed;
  }

  // 已经撤销过：幂等成功
  if (link.revoked_at.has_value()) {
    return Status::Success();
  }

  auto revoked = shares_.Revoke(share_id, link.created_by);
  if (!revoked) {
    return Status(revoked.GetError());
  }
  if (!revoked.Value()) {
    // created_by 不是当前用户（例如管理员代为撤销）：走管理员路径显式撤销
    return Status(Error{"FORBIDDEN", "只有分享的创建者或资源所有者能撤销该链接"});
  }

  AuditEntry entry;
  entry.id = GenerateUuidV4();
  entry.actor_id = actor.user_id;
  entry.action = "revoke_share";
  entry.resource_type = link.resource_type;
  entry.resource_id = link.resource_id;
  entry.detail_json = JsonCompact({{"share_id", link.id}});
  (void)audit_.Record(entry);

  spdlog::info("撤销分享链接 | actor={} | share_id={}", actor.user_id, link.id);
  return Status::Success();
}

Result<PublicShareView> SharingService::DescribePublic(std::string_view token) {
  if (token.empty()) {
    return Error{"INVALID_ARGUMENT", "缺少分享 Token"};
  }
  auto found = shares_.FindUsableByTokenHash(HashShareToken(token));
  if (!found) {
    return found.GetError();
  }
  if (!found.Value().has_value()) {
    // 过期、已撤销、超出次数、Token 不存在，一律同一个错误，不泄露具体原因
    return ShareNotFound();
  }
  const ShareLink link = *found.Value();

  auto resource = Resolve(link.resource_id);
  if (!resource) {
    return resource.GetError();
  }

  PublicShareView view;
  view.name = resource.Value().name;
  view.resource_type = resource.Value().type;
  view.size_bytes = resource.Value().size_bytes;
  view.mime_type = resource.Value().mime_type;
  view.permission = link.permission;
  view.expires_at = link.expires_at;
  view.download_count = link.download_count;
  view.max_downloads = link.max_downloads;
  // 文件夹分享不支持打包下载；这里给出可解释的原因，前端据此隐藏按钮
  view.can_download = resource.Value().type == "file";
  if (!view.can_download) {
    view.download_blocked_reason = "文件夹分享暂不支持打包下载";
  }
  return view;
}

Result<SharingService::PublicDownload> SharingService::PreparePublicDownload(
    std::string_view token) {
  if (token.empty()) {
    return Error{"INVALID_ARGUMENT", "缺少分享 Token"};
  }
  auto found = shares_.FindUsableByTokenHash(HashShareToken(token));
  if (!found) {
    return found.GetError();
  }
  if (!found.Value().has_value()) {
    return ShareNotFound();
  }
  const ShareLink link = *found.Value();

  auto resource = Resolve(link.resource_id);
  if (!resource) {
    return resource.GetError();
  }
  if (resource.Value().type != "file") {
    return Error{"FORBIDDEN", "文件夹分享不支持打包下载"};
  }
  if (!resource.Value().active) {
    return Error{"RESOURCE_NOT_FOUND", "资源已被删除"};
  }

  // 先原子地消费一次下载次数：达到上限时这里会失败，绝不降级放行
  auto consumed = shares_.ConsumeDownload(link.id);
  if (!consumed) {
    return consumed.GetError();
  }
  if (!consumed.Value()) {
    return Error{"FORBIDDEN", "该分享链接的下载次数已用完"};
  }

  auto file = files_.FindById(resource.Value().id);
  if (!file) {
    return file.GetError();
  }
  if (!file.Value().has_value()) {
    return NotFound();
  }
  auto path = storage_.ResolveDownloadPath(file.Value()->storage_key);
  if (!path) {
    spdlog::error("分享下载失败：对象缺失 | file_id={}", resource.Value().id);
    return Error{"RESOURCE_NOT_FOUND", "文件内容缺失，请联系管理员"};
  }

  PublicDownload download;
  download.resource = resource.Value();
  download.file = *file.Value();
  download.absolute_path = path.Value();
  return download;
}

}  // namespace lan_file_hub
