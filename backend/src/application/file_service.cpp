#include "lan_file_hub/application/file_service.h"

#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include "lan_file_hub/application/folder_service.h"
#include "lan_file_hub/common/sql_like.h"
#include "lan_file_hub/common/uuid.h"

namespace lan_file_hub {
namespace {

Error NotFound() {
  return Error{"RESOURCE_NOT_FOUND", "文件不存在"};
}

// 搜索（2026-10-03）：关键字长度上限与"最多回几个文件夹"。
// 文件夹不分页，但不能没有上限 —— 否则用户搜一个常见字会把整个目录树拉回去。
constexpr std::size_t kMaxSearchQueryLength = 100;
constexpr int kMaxSearchFolders = 20;

}  // namespace

// ---- FileUploadSession ------------------------------------------------------

FileUploadSession::FileUploadSession(ObjectStorage& storage, FileRepository& files,
                                     std::unique_ptr<ObjectWriter> writer, std::string owner_id,
                                     std::string folder_id, std::string file_name,
                                     std::string mime_type, std::uint64_t max_bytes)
    : storage_(storage),
      files_(files),
      writer_(std::move(writer)),
      owner_id_(std::move(owner_id)),
      folder_id_(std::move(folder_id)),
      file_name_(std::move(file_name)),
      mime_type_(std::move(mime_type)),
      max_bytes_(max_bytes) {}

FileUploadSession::~FileUploadSession() {
  // 没有正常结束就丢弃临时对象，避免留下孤儿文件
  if (!finished_ && !aborted_) {
    Abort();
  }
}

Status FileUploadSession::Write(const char* data, std::size_t length) {
  if (finished_ || aborted_) {
    return Status(Error{"INVALID_ARGUMENT", "上传会话已结束"});
  }
  if (bytes_written_ + length > max_bytes_) {
    return Status(Error{"INVALID_ARGUMENT",
                        "文件超过单文件大小上限（" + std::to_string(max_bytes_) + " 字节）"});
  }
  if (writer_ == nullptr) {
    return Status(Error{"INTERNAL_ERROR", "上传会话未初始化"});
  }

  auto written = writer_->Write(data, length);
  if (!written.Ok()) {
    return written;
  }
  auto hashed = hasher_.Update(data, length);
  if (!hashed.Ok()) {
    return hashed;
  }
  bytes_written_ += length;
  return Status::Success();
}

Result<File> FileUploadSession::Finish() {
  if (finished_ || aborted_) {
    return Error{"INVALID_ARGUMENT", "上传会话已结束"};
  }
  finished_ = true;

  auto flushed = writer_->Finish();
  if (!flushed.Ok()) {
    Abort();
    return flushed.GetError();
  }

  auto digest = hasher_.HexDigest();
  if (!digest.Ok()) {
    Abort();
    return digest.GetError();
  }
  const std::string content_hash = digest.Value();

  auto final_key = MakeObjectKey(content_hash);
  if (!final_key.Ok()) {
    Abort();
    return Error{"INTERNAL_ERROR", final_key.GetError().message};
  }

  auto committed = storage_.Commit(writer_->temp_key(), final_key.Value());
  if (!committed.Ok()) {
    Abort();
    return committed.GetError();
  }

  File file = MakeFileRecord(owner_id_, folder_id_, file_name_, mime_type_, content_hash,
                             final_key.Value(), bytes_written_);

  FileVersion version;
  version.id = GenerateUuidV4();
  version.file_id = file.id;
  version.version_number = 1;
  version.content_hash = content_hash;
  version.size_bytes = bytes_written_;
  version.storage_key = final_key.Value();
  version.created_by = owner_id_;
  version.is_current = true;

  auto created = files_.CreateWithFirstVersion(file, version);
  if (!created) {
    // 对象已经提交但元数据没落库：保留对象（可能是重复内容，删掉会误伤别的文件），
    // 由后台清理任务按引用关系处理。这里只如实上报失败。
    spdlog::error("上传完成但元数据写入失败 | owner={} | key={} | code={}", owner_id_,
                  final_key.Value(), created.GetError().code);
    return created.GetError();
  }

  spdlog::info("文件上传完成 | owner={} | file_id={} | name={} | bytes={} | sha256={}", owner_id_,
               created.Value().id, file_name_, bytes_written_, content_hash);
  return created.Value();
}

void FileUploadSession::Abort() {
  if (aborted_ || finished_) {
    return;
  }
  aborted_ = true;
  if (writer_ != nullptr) {
    // 关闭句柄后再删，Windows 上文件被占用时删不掉
    (void)writer_->Finish();
    const auto removed = storage_.Delete(writer_->temp_key());
    if (!removed.Ok()) {
      spdlog::warn("清理临时对象失败 | key={}", std::string(writer_->temp_key()));
    }
  }
}

// ---- FileService ------------------------------------------------------------

File MakeFileRecord(std::string_view owner_id, std::string_view folder_id,
                    std::string_view file_name, std::string_view mime_type,
                    std::string_view content_hash, std::string_view storage_key,
                    std::uint64_t size_bytes) {
  File file;
  file.id = GenerateUuidV4();
  file.owner_id = std::string(owner_id);
  file.folder_id = std::string(folder_id);
  file.original_name = std::string(file_name);
  file.mime_type = SanitizeMimeType(mime_type);
  file.size_bytes = size_bytes;
  file.content_hash = std::string(content_hash);
  file.storage_key = std::string(storage_key);
  file.status = FileStatus::kActive;
  return file;
}

FileService::FileService(FileRepository& files, FolderRepository& folders, ObjectStorage& storage,
                         const ResourcePermissionChecker& permissions, FileServiceOptions options)
    : files_(files),
      folders_(folders),
      storage_(storage),
      permissions_(permissions),
      options_(options) {}

Result<std::shared_ptr<FileUploadSession>> FileService::BeginUpload(const Actor& actor,
                                                                    const UploadRequest& request) {
  // 目标目录必须存在且可写
  auto folder = folders_.FindById(request.folder_id);
  if (!folder) {
    return folder.GetError();
  }
  if (!folder.Value().has_value() || !folder.Value()->IsActive()) {
    return Error{"FOLDER_NOT_FOUND", "目标文件夹不存在"};
  }
  const Folder target = *folder.Value();

  auto allowed = permissions_.Can(
      actor.user_id, actor.role,
      ResourceDescriptor{target.owner_id, "folder", target.id, target.parent_id.value_or("")},
      ResourceAction::kWrite);
  if (!allowed) {
    return allowed.GetError();
  }
  if (!allowed.Value()) {
    return Error{"FOLDER_NOT_FOUND", "目标文件夹不存在"};
  }

  auto name = ValidateFileName(request.file_name);
  if (!name) {
    return name.GetError();
  }

  // 同名冲突：阶段四不做覆盖，交给阶段七的版本机制
  auto duplicated = files_.ExistsWithName(target.id, name.Value());
  if (!duplicated) {
    return duplicated.GetError();
  }
  if (duplicated.Value()) {
    return Error{"RESOURCE_CONFLICT", "同一目录下已存在同名文件"};
  }

  // 磁盘空间下限（阶段十二）：流式上传事前不知道总大小，
  // 但至少要保证系统保留空间没被吃光，否则写到一半必然失败
  auto available = storage_.AvailableBytes();
  if (!available) {
    return available.GetError();
  }
  if (available.Value() < options_.min_free_disk_bytes) {
    return Error{"STORAGE_FULL", "服务器磁盘可用空间不足，暂时无法接收上传"};
  }

  auto writer = storage_.CreateWriter();
  if (!writer) {
    return writer.GetError();
  }

  return std::make_shared<FileUploadSession>(storage_, files_, std::move(writer.Value()),
                                             target.owner_id, target.id, name.Value(),
                                             request.mime_type, options_.max_file_size_bytes);
}

Result<FileListing> FileService::ListInFolder(const Actor& actor, std::string_view folder_id,
                                              const Pagination& pagination) {
  auto folder = folders_.FindById(folder_id);
  if (!folder) {
    return folder.GetError();
  }
  if (!folder.Value().has_value() || !folder.Value()->IsActive()) {
    return Error{"FOLDER_NOT_FOUND", "文件夹不存在"};
  }

  auto allowed =
      permissions_.Can(actor.user_id, actor.role,
                       ResourceDescriptor{folder.Value()->owner_id, "folder", folder.Value()->id,
                                          folder.Value()->parent_id.value_or("")},
                       ResourceAction::kRead);
  if (!allowed) {
    return allowed.GetError();
  }
  if (!allowed.Value()) {
    return Error{"FOLDER_NOT_FOUND", "文件夹不存在"};
  }

  // 排序白名单：右边是固定 SQL 片段
  auto order_by = BuildOrderBy(pagination, {
                                               {"name", "lower(f.original_name)"},
                                               {"size", "f.size_bytes"},
                                               {"created_at", "f.created_at"},
                                               {"updated_at", "f.updated_at"},
                                           });
  if (!order_by) {
    return order_by.GetError();
  }

  auto listed = files_.ListByFolder(folder.Value()->id, pagination, order_by.Value());
  if (!listed) {
    return listed.GetError();
  }
  auto total = files_.CountByFolder(folder.Value()->id);
  if (!total) {
    return total.GetError();
  }

  FileListing listing;
  listing.files = std::move(listed.Value());
  listing.total = total.Value();
  listing.page = pagination.page;
  listing.page_size = pagination.page_size;
  return listing;
}

Result<RecentListing> FileService::ListRecent(const Actor& actor, const Pagination& pagination) {
  // 只列自己的文件：不需要权限检查（不是别人的资源），但必须带着 owner 过滤，别越界
  auto listed = files_.ListRecentByOwner(actor.user_id, pagination.page_size, pagination.Offset());
  if (!listed) {
    return listed.GetError();
  }
  auto total = files_.CountRecentByOwner(actor.user_id);
  if (!total) {
    return total.GetError();
  }

  RecentListing listing;
  listing.files = std::move(listed.Value());
  listing.total = total.Value();
  listing.page = pagination.page;
  listing.page_size = pagination.page_size;
  return listing;
}

Result<SearchListing> FileService::Search(const Actor& actor, std::string_view query,
                                          const Pagination& pagination) {
  // 关键字校验：空串直接拒绝（前端的搜索框为空时不该发请求；发了也给一句人话）
  const std::string trimmed = TrimForSearch(query);
  if (trimmed.empty()) {
    return Error{"INVALID_ARGUMENT", "请输入要搜索的关键字"};
  }
  if (trimmed.size() > kMaxSearchQueryLength) {
    return Error{"INVALID_ARGUMENT",
                 "关键字太长（最多 " + std::to_string(kMaxSearchQueryLength) + " 个字符）"};
  }

  // 只搜自己的：不需要权限检查（不是别人的资源），但必须带 owner 过滤，别越界
  auto matched =
      files_.SearchByOwner(actor.user_id, trimmed, pagination.page_size, pagination.Offset());
  if (!matched) {
    return matched.GetError();
  }
  auto total = files_.CountSearchByOwner(actor.user_id, trimmed);
  if (!total) {
    return total.GetError();
  }
  // 文件夹一并返回（数量少、不分页）：搜一个词，文件和目录都能直达
  auto folders = folders_.SearchByOwner(actor.user_id, trimmed, kMaxSearchFolders);
  if (!folders) {
    return folders.GetError();
  }

  SearchListing listing;
  listing.files = std::move(matched.Value());
  listing.folders = std::move(folders.Value());
  listing.total = total.Value();
  listing.page = pagination.page;
  listing.page_size = pagination.page_size;
  listing.query = trimmed;
  return listing;
}

Result<File> FileService::LoadAccessible(const Actor& actor, std::string_view file_id,
                                         ResourceAction action) {
  auto found = files_.FindById(file_id);
  if (!found) {
    return found.GetError();
  }
  if (!found.Value().has_value() || !found.Value()->IsActive()) {
    return NotFound();
  }
  const File file = *found.Value();

  auto allowed =
      permissions_.Can(actor.user_id, actor.role,
                       ResourceDescriptor{file.owner_id, "file", file.id, file.folder_id}, action);
  if (!allowed) {
    return allowed.GetError();
  }
  if (!allowed.Value()) {
    // 无权限与不存在返回同样的错误，避免探测他人资源
    return NotFound();
  }
  return file;
}

Result<File> FileService::GetMetadata(const Actor& actor, std::string_view file_id) {
  return LoadAccessible(actor, file_id, ResourceAction::kRead);
}

Result<File> FileService::GetMetadataIncludingTrashed(const Actor& actor,
                                                      std::string_view file_id) {
  // 回收站里的文件已经不是 ACTIVE，但用户仍应能看到它的名字与删除时间
  auto found = files_.FindById(file_id);
  if (!found) {
    return found.GetError();
  }
  if (!found.Value().has_value()) {
    return NotFound();
  }
  const File file = *found.Value();
  auto allowed = permissions_.Can(
      actor.user_id, actor.role, ResourceDescriptor{file.owner_id, "file", file.id, file.folder_id},
      ResourceAction::kRead);
  if (!allowed) {
    return allowed.GetError();
  }
  if (!allowed.Value()) {
    return NotFound();
  }
  return file;
}

Result<FileDownload> FileService::PrepareDownload(const Actor& actor, std::string_view file_id) {
  auto file = LoadAccessible(actor, file_id, ResourceAction::kRead);
  if (!file) {
    return file.GetError();
  }

  auto path = storage_.ResolveDownloadPath(file.Value().storage_key);
  if (!path) {
    // 元数据存在但对象缺失：必须记录，不能静默返回空内容
    spdlog::error("元数据存在但对象缺失 | file_id={} | storage_key={}", file.Value().id,
                  file.Value().storage_key);
    return Error{"RESOURCE_NOT_FOUND", "文件内容缺失，请联系管理员"};
  }

  FileDownload download;
  download.file = file.Value();
  download.absolute_path = path.Value();
  return download;
}

// ---- 版本与乐观并发（阶段七）------------------------------------------------

Result<std::vector<FileVersion>> FileService::ListVersions(const Actor& actor,
                                                           std::string_view file_id) {
  auto file = LoadAccessible(actor, file_id, ResourceAction::kRead);
  if (!file) {
    return file.GetError();
  }
  return files_.ListVersions(file.Value().id);
}

Result<VersionDownload> FileService::PrepareVersionDownload(const Actor& actor,
                                                            std::string_view file_id,
                                                            std::string_view version_id) {
  auto file = LoadAccessible(actor, file_id, ResourceAction::kRead);
  if (!file) {
    return file.GetError();
  }

  auto version = files_.FindVersion(file.Value().id, version_id);
  if (!version) {
    return version.GetError();
  }
  if (!version.Value().has_value()) {
    return Error{"RESOURCE_NOT_FOUND", "指定的历史版本不存在"};
  }

  auto path = storage_.ResolveDownloadPath(version.Value()->storage_key);
  if (!path) {
    spdlog::error("历史版本对象缺失 | file_id={} | version_id={} | storage_key={}", file.Value().id,
                  version.Value()->id, version.Value()->storage_key);
    return Error{"RESOURCE_NOT_FOUND", "该版本的内容缺失，请联系管理员"};
  }

  VersionDownload download;
  download.file = file.Value();
  download.version = *version.Value();
  download.absolute_path = path.Value();
  return download;
}

Result<File> FileService::RestoreVersion(const Actor& actor, std::string_view file_id,
                                         std::string_view version_id,
                                         std::optional<std::string> expected_version_id) {
  // 恢复会改变文件内容，需要写权限
  auto file = LoadAccessible(actor, file_id, ResourceAction::kWrite);
  if (!file) {
    return file.GetError();
  }

  auto target = files_.FindVersion(file.Value().id, version_id);
  if (!target) {
    return target.GetError();
  }
  if (!target.Value().has_value()) {
    return Error{"RESOURCE_NOT_FOUND", "指定的历史版本不存在"};
  }
  const FileVersion restored_from = *target.Value();

  // 重复恢复必须幂等：当前内容与目标版本一致时不再追加一个一模一样的版本
  auto current = files_.FindCurrentVersion(file.Value().id);
  if (!current) {
    return current.GetError();
  }
  if (current.Value().has_value() && current.Value()->content_hash == restored_from.content_hash &&
      current.Value()->size_bytes == restored_from.size_bytes) {
    spdlog::info("恢复历史版本为幂等空操作 | file_id={} | version_number={}", file.Value().id,
                 restored_from.version_number);
    return file.Value();
  }

  // 对象必须还在：元数据与磁盘不一致要报出来，而不是写出一个指向空内容的版本
  if (!storage_.Exists(restored_from.storage_key)) {
    spdlog::error("恢复失败：历史版本对象缺失 | file_id={} | version_id={} | storage_key={}",
                  file.Value().id, restored_from.id, restored_from.storage_key);
    return Error{"RESOURCE_NOT_FOUND", "该版本的内容缺失，请联系管理员"};
  }

  FileVersion version;
  version.id = GenerateUuidV4();
  version.file_id = file.Value().id;
  version.content_hash = restored_from.content_hash;
  version.size_bytes = restored_from.size_bytes;
  version.storage_key = restored_from.storage_key;
  version.created_by = actor.user_id;
  version.is_current = true;

  auto updated = files_.AddVersion(file.Value().id, version, expected_version_id);
  if (!updated) {
    return updated.GetError();
  }

  spdlog::info("恢复历史版本 | file_id={} | from_version={} | new_version_id={} | user={}",
               file.Value().id, restored_from.version_number, version.id, actor.user_id);
  return updated.Value();
}

}  // namespace lan_file_hub
