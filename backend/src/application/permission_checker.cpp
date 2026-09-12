#include "lan_file_hub/application/permission_checker.h"

#include <algorithm>
#include <vector>

#include "lan_file_hub/infrastructure/file_repository.h"
#include "lan_file_hub/infrastructure/folder_repository.h"
#include "lan_file_hub/infrastructure/permission_repository.h"

namespace lan_file_hub {

const char* ToString(ResourceAction action) {
  switch (action) {
    case ResourceAction::kWrite:
      return "WRITE";
    case ResourceAction::kDelete:
      return "DELETE";
    case ResourceAction::kManage:
      return "MANAGE";
    case ResourceAction::kRead:
    default:
      return "READ";
  }
}

std::string ToDbString(PermissionLevel level) {
  switch (level) {
    case PermissionLevel::kOwner:
      return "OWNER";
    case PermissionLevel::kWrite:
      return "WRITE";
    case PermissionLevel::kRead:
      return "READ";
    case PermissionLevel::kNone:
    default:
      return "NONE";
  }
}

PermissionLevel ParsePermissionLevel(const std::string& value) {
  if (value == "OWNER") {
    return PermissionLevel::kOwner;
  }
  if (value == "WRITE") {
    return PermissionLevel::kWrite;
  }
  if (value == "READ") {
    return PermissionLevel::kRead;
  }
  return PermissionLevel::kNone;
}

PermissionLevel RequiredLevel(ResourceAction action) {
  switch (action) {
    case ResourceAction::kManage:
      // 永久删除、改权限、建分享链接都要求 OWNER
      return PermissionLevel::kOwner;
    case ResourceAction::kWrite:
    case ResourceAction::kDelete:
      // WRITE 包含读取，并允许上传、更新、移动、重命名与（软）删除
      return PermissionLevel::kWrite;
    case ResourceAction::kRead:
    default:
      return PermissionLevel::kRead;
  }
}

bool Satisfies(PermissionLevel level, ResourceAction action) {
  return static_cast<int>(level) >= static_cast<int>(RequiredLevel(action));
}

Result<PermissionLevel> ResourcePermissionChecker::EffectiveLevel(
    std::string_view actor_id, const ResourceDescriptor& resource) const {
  if (actor_id.empty()) {
    return PermissionLevel::kNone;
  }
  if (!resource.owner_id.empty() && actor_id == resource.owner_id) {
    return PermissionLevel::kOwner;
  }
  return PermissionLevel::kNone;
}

Result<bool> OwnerBasedPermissionChecker::Can(std::string_view actor_id, UserRole actor_role,
                                              const ResourceDescriptor& resource,
                                              ResourceAction /*action*/) const {
  if (actor_id.empty()) {
    return false;
  }
  // 所有者对自己的一切资源都有权限
  if (actor_id == resource.owner_id) {
    return true;
  }
  // 管理员可以操作，但调用方必须留下审计记录（docs/architecture.md §管理员权限边界）
  if (actor_role == UserRole::kAdmin) {
    return true;
  }
  return false;
}

// ---- GrantBasedPermissionChecker --------------------------------------------

GrantBasedPermissionChecker::GrantBasedPermissionChecker(PermissionRepository& permissions,
                                                         FolderRepository& folders,
                                                         FileRepository& files)
    : permissions_(permissions), folders_(folders), files_(files) {}

Result<std::string> GrantBasedPermissionChecker::PathKeyFor(
    const ResourceDescriptor& resource) const {
  if (resource.resource_type == "folder") {
    if (resource.resource_id.empty()) {
      return std::string();
    }
    auto path = folders_.GetPathKey(resource.resource_id);
    if (!path) {
      return path.GetError();
    }
    return path.Value();
  }

  // 文件：继承所属目录。资源描述里没带目录时回查一次，避免调用方漏传。
  std::string folder_id(resource.folder_id);
  if (folder_id.empty() && resource.resource_type == "file" && !resource.resource_id.empty()) {
    auto file = files_.FindById(resource.resource_id);
    if (!file) {
      return file.GetError();
    }
    if (!file.Value().has_value()) {
      return std::string();
    }
    folder_id = file.Value()->folder_id;
  }
  if (folder_id.empty()) {
    return std::string();
  }
  auto path = folders_.GetPathKey(folder_id);
  if (!path) {
    return path.GetError();
  }
  return path.Value();
}

Result<PermissionLevel> GrantBasedPermissionChecker::EffectiveLevel(
    std::string_view actor_id, const ResourceDescriptor& resource) const {
  if (actor_id.empty()) {
    return PermissionLevel::kNone;
  }

  std::vector<PermissionLevel> levels;

  // 1) 资源自身的直接授权
  if (!resource.resource_type.empty() && !resource.resource_id.empty()) {
    auto direct = permissions_.FindDirect(resource.resource_type, resource.resource_id, actor_id);
    if (!direct) {
      return direct.GetError();
    }
    if (direct.Value().has_value()) {
      levels.push_back(ParsePermissionLevel(*direct.Value()));
    }
  }

  // 2) 祖先文件夹的授权（含文件所在目录自身）
  auto path = PathKeyFor(resource);
  if (!path) {
    return path.GetError();
  }
  if (!path.Value().empty()) {
    auto inherited = permissions_.ListLevelsOnPath(actor_id, path.Value());
    if (!inherited) {
      return inherited.GetError();
    }
    for (const auto& value : inherited.Value()) {
      levels.push_back(ParsePermissionLevel(value));
    }
  }

  if (levels.empty()) {
    return PermissionLevel::kNone;
  }
  return *std::max_element(levels.begin(), levels.end());
}

Result<bool> GrantBasedPermissionChecker::Can(std::string_view actor_id, UserRole actor_role,
                                              const ResourceDescriptor& resource,
                                              ResourceAction action) const {
  if (actor_id.empty()) {
    return false;
  }
  // 管理员是系统级权限；调用方负责写审计日志（docs/permissions.md §2.1）
  if (actor_role == UserRole::kAdmin) {
    return true;
  }
  // 资源所有者等价于 OWNER
  if (!resource.owner_id.empty() && actor_id == resource.owner_id) {
    return true;
  }
  auto level = EffectiveLevel(actor_id, resource);
  if (!level) {
    return level.GetError();
  }
  return Satisfies(level.Value(), action);
}

}  // namespace lan_file_hub
