#include "lan_file_hub/application/trash_service.h"

#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "lan_file_hub/application/folder_service.h"  // Actor
#include "lan_file_hub/common/uuid.h"

namespace lan_file_hub {
namespace {

Error NotFound() {
  return Error{"RESOURCE_NOT_FOUND", "资源不存在或已被永久删除"};
}

std::string JsonCompact(const nlohmann::json& value) {
  return value.dump();
}

}  // namespace

TrashService::TrashService(TrashRepository& trash, FileRepository& files, FolderRepository& folders,
                           AuditLogRepository& audit, CleanupTaskRepository& cleanup,
                           PermissionRepository& permissions_repo, ShareLinkRepository& shares,
                           const ResourcePermissionChecker& permissions,
                           TrashServiceOptions options)
    : trash_(trash),
      files_(files),
      folders_(folders),
      audit_(audit),
      cleanup_(cleanup),
      permissions_repo_(permissions_repo),
      shares_(shares),
      permissions_(permissions),
      options_(options) {}

Status TrashService::Audit(std::string_view actor_id, std::string_view action,
                           std::string_view resource_type, std::string_view resource_id,
                           const std::string& detail_json) {
  AuditEntry entry;
  entry.id = GenerateUuidV4();
  entry.actor_id = std::string(actor_id);
  entry.action = std::string(action);
  entry.resource_type = std::string(resource_type);
  entry.resource_id = std::string(resource_id);
  entry.detail_json = detail_json;
  auto status = audit_.Record(entry);
  if (!status.Ok()) {
    // 审计写失败不能把业务判成失败，但必须显式记录，不能静默
    spdlog::error("审计日志写入失败 | action={} | resource_id={} | error={}", action, resource_id,
                  status.GetError().detail);
  }
  return status;
}

Result<TrashListing> TrashService::List(const Actor& actor, const Pagination& pagination,
                                        std::string_view kind_filter) {
  // 普通用户只看自己的；管理员可以看全部（owner 传空）
  const std::string owner_id = actor.role == UserRole::kAdmin ? std::string() : actor.user_id;
  return trash_.List(owner_id, pagination, "deleted_at DESC, name ASC", kind_filter);
}

Result<RestoreOutcome> TrashService::Restore(const Actor& actor, std::string_view resource_id) {
  const std::string owner_id = actor.role == UserRole::kAdmin ? std::string() : actor.user_id;

  auto found = trash_.FindItem(owner_id, resource_id);
  if (!found) {
    return found.GetError();
  }
  if (!found.Value().has_value()) {
    // 不在自己的回收站里：可能是「已经恢复过」（幂等成功），
    // 也可能是别人的资源或根本不存在——后者必须返回 404，不能给出成功响应。
    auto active = trash_.IsActiveOwnedBy(owner_id, resource_id);
    if (!active) {
      return active.GetError();
    }
    if (active.Value()) {
      RestoreOutcome outcome;
      outcome.item.id = std::string(resource_id);
      outcome.already_restored = true;
      spdlog::info("恢复请求命中幂等分支（资源已是 ACTIVE） | actor={} | resource_id={}",
                   actor.user_id, resource_id);
      return outcome;
    }
    return NotFound();
  }

  const TrashItem item = *found.Value();

  RestoreOutcome outcome;
  outcome.item = item;

  if (item.kind == "file") {
    // 原目录还在就放回原处，否则回到用户根目录
    bool target_ok = !item.folder_id.empty();
    if (target_ok) {
      auto available = trash_.FolderAvailable(item.folder_id);
      if (!available) {
        return available.GetError();
      }
      target_ok = available.Value();
    }

    std::string target_folder_id = item.folder_id;
    if (!target_ok) {
      auto root = trash_.RootFolderId(item.owner_id);
      if (!root) {
        return root.GetError();
      }
      if (!root.Value().has_value()) {
        return Error{"FOLDER_NOT_FOUND", "用户根目录不存在，无法恢复"};
      }
      target_folder_id = *root.Value();
      outcome.moved_to_root = true;
    }

    // 同名冲突：恢复后同一目录下会出现两个同名文件，必须拒绝并说明
    auto conflict = trash_.ActiveNameExists(target_folder_id, item.name, item.id);
    if (!conflict) {
      return conflict.GetError();
    }
    if (conflict.Value()) {
      return Error::WithDetails(
          "RESOURCE_CONFLICT", "目标目录下已有同名文件「" + item.name + "」，请先重命名或删除它",
          {{"conflict_name", item.name}, {"target_folder_id", target_folder_id}});
    }

    auto restored = trash_.RestoreFile(item.id, target_folder_id);
    if (!restored.Ok()) {
      return restored.GetError();
    }
    outcome.target_folder_id = target_folder_id;
    (void)Audit(actor.user_id, "restore_file", "file", item.id,
                JsonCompact({{"name", item.name}, {"moved_to_root", outcome.moved_to_root}}));
    spdlog::info("恢复文件 | actor={} | file_id={} | name={} | moved_to_root={}", actor.user_id,
                 item.id, item.name, outcome.moved_to_root);
    return outcome;
  }

  if (item.kind == "folder") {
    // 文件夹恢复到原父目录；父目录不在（或被删）时恢复到根目录
    bool target_ok = !item.folder_id.empty();
    if (target_ok) {
      auto available = trash_.FolderAvailable(item.folder_id);
      if (!available) {
        return available.GetError();
      }
      target_ok = available.Value();
    }
    if (!target_ok) {
      auto root = trash_.RootFolderId(item.owner_id);
      if (!root) {
        return root.GetError();
      }
      if (!root.Value().has_value()) {
        return Error{"FOLDER_NOT_FOUND", "用户根目录不存在，无法恢复"};
      }
      outcome.moved_to_root = true;
      // 移动：parent_id 换成根目录，并重写整棵子树的 materialized path
      auto root_path = folders_.GetPathKey(*root.Value());
      if (!root_path) {
        return root_path.GetError();
      }
      auto moved = folders_.Move(item.id, *root.Value(), root_path.Value() + item.id + "/");
      if (!moved.Ok()) {
        return moved.GetError();
      }
      outcome.target_folder_id = *root.Value();
    } else {
      outcome.target_folder_id = item.folder_id;
    }

    auto conflict = trash_.ActiveFolderNameExists(outcome.target_folder_id, item.name, item.id);
    if (!conflict) {
      return conflict.GetError();
    }
    if (conflict.Value()) {
      return Error::WithDetails(
          "RESOURCE_CONFLICT", "目标目录下已有同名文件夹「" + item.name + "」",
          {{"conflict_name", item.name}, {"target_folder_id", outcome.target_folder_id}});
    }

    auto restored = trash_.RestoreFolderSubtree(item.id);
    if (!restored) {
      return restored.GetError();
    }
    (void)Audit(actor.user_id, "restore_folder", "folder", item.id,
                JsonCompact({{"name", item.name},
                             {"moved_to_root", outcome.moved_to_root},
                             {"folders", restored.Value()}}));
    spdlog::info("恢复文件夹 | actor={} | folder_id={} | name={} | moved_to_root={}", actor.user_id,
                 item.id, item.name, outcome.moved_to_root);
    return outcome;
  }

  return NotFound();
}

Result<PurgeOutcome> TrashService::Purge(const Actor& actor, std::string_view resource_id) {
  const std::string owner_id = actor.role == UserRole::kAdmin ? std::string() : actor.user_id;

  auto found = trash_.FindItem(owner_id, resource_id);
  if (!found) {
    return found.GetError();
  }
  if (!found.Value().has_value()) {
    return NotFound();
  }
  const TrashItem item = *found.Value();

  PurgeOutcome outcome;
  outcome.resource_id = item.id;
  outcome.kind = item.kind;

  PurgeResult purged;
  if (item.kind == "file") {
    auto result = trash_.PurgeFile(item.id);
    if (!result) {
      return result.GetError();
    }
    purged = result.Value();
  } else {
    // 保护根目录：永远不能被永久删除
    if (item.folder_id.empty()) {
      return Error{"FORBIDDEN", "根目录不能永久删除"};
    }
    auto result = trash_.PurgeFolder(item.id);
    if (!result) {
      return result.GetError();
    }
    purged = result.Value();
  }

  outcome.freed_bytes = purged.freed_bytes;
  outcome.file_count = purged.file_count;
  outcome.folder_count = purged.folder_count;

  // 资源已经不存在了，指向它的授权与分享链接必须一起清掉，
  // 否则会留下悬空记录（多态引用没有外键，只能显式删）
  (void)permissions_repo_.DeleteForResource(item.kind, item.id);
  (void)shares_.DeleteForResource(item.kind, item.id);
  if (item.kind == "folder" && !purged.path_key_prefix.empty()) {
    (void)permissions_repo_.DeleteForFolderSubtree(purged.path_key_prefix);
    (void)shares_.DeleteForFolderSubtree(purged.path_key_prefix);
  }

  // 对象删除交给后台任务：HTTP 请求不做长时间磁盘操作，失败还能重试
  for (const auto& key : purged.storage_keys) {
    auto queued = cleanup_.Enqueue(key, "purge");
    if (!queued.Ok()) {
      spdlog::error("对象清理任务入队失败 | key={} | error={}", key, queued.GetError().detail);
      continue;
    }
    outcome.queued_objects += 1;
  }

  (void)Audit(actor.user_id, item.kind == "file" ? "purge_file" : "purge_folder", item.kind,
              item.id,
              JsonCompact({{"name", item.name},
                           {"freed_bytes", outcome.freed_bytes},
                           {"files", outcome.file_count},
                           {"folders", outcome.folder_count},
                           {"queued_objects", outcome.queued_objects}}));
  spdlog::info(
      "永久删除 | actor={} | kind={} | id={} | files={} | folders={} | freed={} | "
      "queued_objects={}",
      actor.user_id, item.kind, item.id, outcome.file_count, outcome.folder_count,
      outcome.freed_bytes, outcome.queued_objects);
  return outcome;
}

Status TrashService::SoftDeleteFile(const Actor& actor, std::string_view file_id) {
  auto file = files_.FindById(file_id);
  if (!file) {
    return Status(file.GetError());
  }
  if (!file.Value().has_value()) {
    // 已经删掉了：幂等成功
    return Status::Success();
  }
  const File& record = *file.Value();
  if (record.status == FileStatus::kTrashed) {
    return Status::Success();
  }

  // 权限：只有能看到这个文件的人才能删；他人资源一律当作不存在
  auto file_owner = files_.FindOwnerId(file_id);
  if (!file_owner) {
    return Status(file_owner.GetError());
  }
  if (!file_owner.Value().has_value()) {
    return Status(NotFound());
  }
  auto allowed =
      permissions_.Can(actor.user_id, actor.role,
                       ResourceDescriptor{*file_owner.Value(), "file", record.id, record.folder_id},
                       ResourceAction::kDelete);
  if (!allowed) {
    return Status(allowed.GetError());
  }
  if (!allowed.Value()) {
    return Status(NotFound());
  }
  auto deleted = files_.SoftDelete(file_id);
  if (!deleted.Ok()) {
    return deleted;
  }
  (void)Audit(actor.user_id, "delete_file", "file", record.id,
              JsonCompact({{"name", record.original_name}, {"size_bytes", record.size_bytes}}));
  spdlog::info("删除文件（软删除） | actor={} | file_id={} | name={}", actor.user_id, record.id,
               record.original_name);
  return Status::Success();
}

Result<std::size_t> TrashService::PurgeExpired(int limit) {
  auto expired = trash_.ListExpired(options_.retention_seconds, limit);
  if (!expired) {
    return expired.GetError();
  }

  std::size_t purged = 0;
  for (const auto& item : expired.Value()) {
    PurgeResult result;
    if (item.kind == "file") {
      auto done = trash_.PurgeFile(item.id);
      if (!done) {
        spdlog::warn("过期回收站清理失败 | id={} | error={}", item.id, done.GetError().detail);
        continue;
      }
      result = done.Value();
    } else {
      if (item.folder_id.empty()) {
        continue;  // 根目录不删
      }
      auto done = trash_.PurgeFolder(item.id);
      if (!done) {
        spdlog::warn("过期回收站清理失败 | id={} | error={}", item.id, done.GetError().detail);
        continue;
      }
      result = done.Value();
    }

    std::size_t queued = 0;
    for (const auto& key : result.storage_keys) {
      if (cleanup_.Enqueue(key, "trash-expired").Ok()) {
        queued += 1;
      }
    }
    (void)Audit(
        "", "purge_expired", item.kind, item.id,
        JsonCompact(
            {{"name", item.name}, {"files", result.file_count}, {"queued_objects", queued}}));
    purged += 1;
  }

  if (purged > 0) {
    spdlog::info("回收站过期清理完成 | purged={} | retention_seconds={}", purged,
                 options_.retention_seconds);
  }
  return purged;
}

}  // namespace lan_file_hub
