#include "lan_file_hub/application/folder_service.h"

#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include "lan_file_hub/common/uuid.h"

namespace lan_file_hub {
namespace {

Error NotFound() {
  return Error{"RESOURCE_NOT_FOUND", "文件夹不存在"};
}

}  // namespace

FolderService::FolderService(FolderRepository& folders,
                             const ResourcePermissionChecker& permissions)
    : folders_(folders), permissions_(permissions) {}

Result<Folder> FolderService::EnsureRootFolder(const Actor& actor) {
  if (actor.user_id.empty()) {
    return Error{"UNAUTHENTICATED", "缺少用户标识"};
  }
  return folders_.EnsureRoot(actor.user_id);
}

Result<Folder> FolderService::LoadAccessible(const Actor& actor, std::string_view folder_id,
                                             ResourceAction action) {
  auto found = folders_.FindById(folder_id);
  if (!found) {
    return found.GetError();
  }
  if (!found.Value().has_value() || !found.Value()->IsActive()) {
    // 已删除的文件夹在普通接口里等同于不存在
    return NotFound();
  }
  const Folder folder = *found.Value();

  auto allowed = permissions_.Can(
      actor.user_id, actor.role,
      ResourceDescriptor{folder.owner_id, "folder", folder.id, folder.parent_id.value_or("")},
      action);
  if (!allowed) {
    return allowed.GetError();
  }
  if (!allowed.Value()) {
    // 无权限与不存在返回同样的错误，避免通过状态码探测他人资源
    return NotFound();
  }
  return folder;
}

Result<Folder> FolderService::Create(const Actor& actor, std::string_view parent_id,
                                     std::string_view name) {
  auto parent = LoadAccessible(actor, parent_id, ResourceAction::kWrite);
  if (!parent) {
    return parent.GetError();
  }

  auto validated = ValidateFolderName(name);
  if (!validated) {
    return validated.GetError();
  }

  Folder folder;
  folder.id = GenerateUuidV4();
  folder.owner_id = parent.Value().owner_id;
  folder.parent_id = parent.Value().id;
  folder.name = validated.Value();
  folder.path_key = parent.Value().path_key + folder.id + "/";
  folder.status = FolderStatus::kActive;

  auto created = folders_.Create(folder);
  if (!created) {
    return created.GetError();
  }

  spdlog::info("创建文件夹 | actor={} | folder_id={} | parent_id={} | name={}", actor.user_id,
               created.Value().id, parent.Value().id, created.Value().name);
  return created.Value();
}

Result<FolderListing> FolderService::ListChildren(const Actor& actor, std::string_view folder_id,
                                                  const Pagination& pagination) {
  auto folder = LoadAccessible(actor, folder_id, ResourceAction::kRead);
  if (!folder) {
    return folder.GetError();
  }

  // 排序白名单：右边是固定的 SQL 片段，绝不接受用户输入直接拼接
  auto order_by = BuildOrderBy(pagination, {
                                               {"name", "lower(f.name)"},
                                               {"created_at", "f.created_at"},
                                               {"updated_at", "f.updated_at"},
                                           });
  if (!order_by) {
    return order_by.GetError();
  }

  auto children = folders_.ListChildren(folder.Value().id, pagination, order_by.Value());
  if (!children) {
    return children.GetError();
  }
  auto total = folders_.CountChildren(folder.Value().id);
  if (!total) {
    return total.GetError();
  }

  FolderListing listing;
  listing.folders = std::move(children.Value());
  listing.total = total.Value();
  listing.page = pagination.page;
  listing.page_size = pagination.page_size;
  return listing;
}

Result<Folder> FolderService::Get(const Actor& actor, std::string_view folder_id) {
  return LoadAccessible(actor, folder_id, ResourceAction::kRead);
}

Result<std::vector<Folder>> FolderService::GetPath(const Actor& actor, std::string_view folder_id) {
  auto folder = LoadAccessible(actor, folder_id, ResourceAction::kRead);
  if (!folder) {
    return folder.GetError();
  }
  return folders_.ListAncestors(folder.Value().path_key);
}

Result<Folder> FolderService::Rename(const Actor& actor, std::string_view folder_id,
                                     std::string_view new_name) {
  auto folder = LoadAccessible(actor, folder_id, ResourceAction::kWrite);
  if (!folder) {
    return folder.GetError();
  }
  if (folder.Value().IsRoot()) {
    return Error{"FORBIDDEN", "根目录不能重命名"};
  }

  auto validated = ValidateFolderName(new_name);
  if (!validated) {
    return validated.GetError();
  }

  auto status = folders_.UpdateName(folder.Value().id, validated.Value());
  if (!status.Ok()) {
    return status.GetError();
  }

  spdlog::info("重命名文件夹 | actor={} | folder_id={} | old={} | new={}", actor.user_id,
               folder.Value().id, folder.Value().name, validated.Value());

  auto updated = folders_.FindById(folder.Value().id);
  if (!updated) {
    return updated.GetError();
  }
  if (!updated.Value().has_value()) {
    return NotFound();
  }
  return *updated.Value();
}

Result<Folder> FolderService::Move(const Actor& actor, std::string_view folder_id,
                                   std::string_view new_parent_id) {
  auto folder = LoadAccessible(actor, folder_id, ResourceAction::kWrite);
  if (!folder) {
    return folder.GetError();
  }
  if (folder.Value().IsRoot()) {
    return Error{"FORBIDDEN", "根目录不能移动"};
  }

  auto target = LoadAccessible(actor, new_parent_id, ResourceAction::kWrite);
  if (!target) {
    return target.GetError();
  }

  // 不能移动到自身
  if (target.Value().id == folder.Value().id) {
    return Error{"INVALID_ARGUMENT", "不能把文件夹移动到它自己"};
  }
  // 不能移动到自己的子树里：目标路径以源路径为前缀即说明目标是源的后代
  if (target.Value().path_key.rfind(folder.Value().path_key, 0) == 0) {
    return Error{"INVALID_ARGUMENT", "不能把文件夹移动到它自己的子目录"};
  }
  // 阶段三不支持跨用户移动
  if (target.Value().owner_id != folder.Value().owner_id) {
    return Error{"FORBIDDEN", "不能移动到其他用户的目录"};
  }

  const std::string new_prefix = target.Value().path_key + folder.Value().id + "/";
  auto status = folders_.Move(folder.Value().id, target.Value().id, new_prefix);
  if (!status.Ok()) {
    return status.GetError();
  }

  spdlog::info("移动文件夹 | actor={} | folder_id={} | from_parent={} | to_parent={}",
               actor.user_id, folder.Value().id, folder.Value().parent_id.value_or("<root>"),
               target.Value().id);

  auto updated = folders_.FindById(folder.Value().id);
  if (!updated) {
    return updated.GetError();
  }
  if (!updated.Value().has_value()) {
    return NotFound();
  }
  return *updated.Value();
}

Status FolderService::SoftDelete(const Actor& actor, std::string_view folder_id) {
  auto folder = LoadAccessible(actor, folder_id, ResourceAction::kDelete);
  if (!folder) {
    return Status(folder.GetError());
  }
  if (folder.Value().IsRoot()) {
    return Status(Error{"FORBIDDEN", "根目录不能删除"});
  }

  auto status = folders_.SoftDeleteSubtree(folder.Value().id);
  if (!status.Ok()) {
    return status;
  }

  spdlog::info("删除文件夹（软删除，含子树） | actor={} | folder_id={} | name={}", actor.user_id,
               folder.Value().id, folder.Value().name);
  return Status::Success();
}

}  // namespace lan_file_hub
