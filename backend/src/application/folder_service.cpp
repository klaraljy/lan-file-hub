#include "lan_file_hub/application/folder_service.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "lan_file_hub/common/uuid.h"

namespace lan_file_hub {
namespace {

Error NotFound() {
  return Error{"RESOURCE_NOT_FOUND", "文件夹不存在"};
}

}  // namespace

FolderService::FolderService(FolderRepository& folders, FileRepository& files,
                             const ResourcePermissionChecker& permissions)
    : folders_(folders), files_(files), permissions_(permissions) {}

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

// 目录（含子目录）里的全部文件（2026-10-03："一同下载文件夹里的东西"）。
//
// 为什么返回扁平清单而不是让前端自己逐层翻：一个是查询，另一个是几百次请求；
// 前端拿到 file_id 后逐个走既有的"一次性票据 + 浏览器原生下载"，不打包、不占额外磁盘。
Result<FolderFileList> FolderService::ListFilesRecursive(const Actor& actor,
                                                         std::string_view folder_id) {
  auto folder = LoadAccessible(actor, folder_id, ResourceAction::kRead);
  if (!folder) {
    return folder.GetError();
  }
  auto path_key = folders_.GetPathKey(folder.Value().id);
  if (!path_key) {
    return path_key.GetError();
  }
  // ListSubtree 按 path_key 排序：父目录一定在子目录之前，所以一趟就能算出相对路径
  auto subtree = folders_.ListSubtree(path_key.Value());
  if (!subtree) {
    return subtree.GetError();
  }

  std::vector<std::string> folder_ids;
  folder_ids.reserve(subtree.Value().size());
  std::map<std::string, std::string> relative_paths;  // 目录 id → 相对被点目录的路径（自己＝空）
  for (const Folder& node : subtree.Value()) {
    folder_ids.push_back(node.id);
    if (node.id == folder.Value().id) {
      relative_paths[node.id] = "";
      continue;
    }
    std::string parent_relative;
    if (node.parent_id.has_value()) {
      const auto parent = relative_paths.find(*node.parent_id);
      if (parent != relative_paths.end()) {
        parent_relative = parent->second;
      }
    }
    relative_paths[node.id] =
        parent_relative.empty() ? node.name : parent_relative + "/" + node.name;
  }

  auto files = files_.ListActiveByFolderIds(folder_ids);
  if (!files) {
    return files.GetError();
  }

  FolderFileList listing;
  listing.file_count = files.Value().size();
  for (const File& file : files.Value()) {
    listing.total_bytes += file.size_bytes;
  }
  const std::size_t take = std::min(files.Value().size(), kFolderFileListLimit);
  listing.truncated = files.Value().size() > take;
  listing.items.reserve(take);
  for (std::size_t index = 0; index < take; ++index) {
    const File& file = files.Value()[index];
    const auto relative = relative_paths.find(file.folder_id);
    const std::string directory = relative != relative_paths.end() ? relative->second : "";
    FolderFileEntry entry;
    entry.file_id = file.id;
    entry.name = file.original_name;
    entry.path = directory.empty() ? file.original_name : directory + "/" + file.original_name;
    entry.size_bytes = file.size_bytes;
    listing.items.push_back(std::move(entry));
  }
  std::sort(listing.items.begin(), listing.items.end(),
            [](const FolderFileEntry& left, const FolderFileEntry& right) {
              return left.path < right.path;
            });
  return listing;
}

}  // namespace lan_file_hub
