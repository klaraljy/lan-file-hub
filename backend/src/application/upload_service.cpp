#include "lan_file_hub/application/upload_service.h"

#include <algorithm>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include "lan_file_hub/application/file_service.h"
#include "lan_file_hub/application/folder_service.h"
#include "lan_file_hub/common/uuid.h"

namespace lan_file_hub {
namespace {

Error UploadNotFound() {
  return Error{"UPLOAD_NOT_FOUND", "上传会话不存在"};
}

}  // namespace

// ---- ChunkUploadSession -----------------------------------------------------

ChunkUploadSession::ChunkUploadSession(ObjectStorage& storage, UploadSessionRepository& sessions,
                                       std::string upload_id, std::uint32_t chunk_index,
                                       std::optional<std::string> expected_chunk_hash,
                                       std::uint64_t max_chunk_bytes)
    : storage_(storage),
      sessions_(sessions),
      upload_id_(std::move(upload_id)),
      chunk_index_(chunk_index),
      expected_chunk_hash_(std::move(expected_chunk_hash)),
      max_chunk_bytes_(max_chunk_bytes) {}

ChunkUploadSession::~ChunkUploadSession() {
  if (!finished_ && !aborted_) {
    Abort();
  }
}

Status ChunkUploadSession::Write(const char* data, std::size_t length) {
  if (finished_ || aborted_) {
    return Status(Error{"INVALID_ARGUMENT", "分块写入已结束"});
  }
  if (bytes_written_ + length > max_chunk_bytes_) {
    return Status(Error{"CHUNK_INVALID", "分块超过允许的大小"});
  }

  if (writer_ == nullptr) {
    auto created = storage_.CreateChunkWriter(upload_id_, chunk_index_);
    if (!created) {
      return Status(created.GetError());
    }
    writer_ = std::move(created.Value());
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

Status ChunkUploadSession::Finish() {
  if (finished_ || aborted_) {
    return Status(Error{"INVALID_ARGUMENT", "分块写入已结束"});
  }

  // 空分块是合法的（空文件只有一个 0 字节分块），所以这里不拦
  if (writer_ == nullptr) {
    auto created = storage_.CreateChunkWriter(upload_id_, chunk_index_);
    if (!created) {
      return Status(created.GetError());
    }
    writer_ = std::move(created.Value());
  }

  auto flushed = writer_->Finish();
  if (!flushed.Ok()) {
    Abort();
    return flushed;
  }

  auto digest = hasher_.HexDigest();
  if (!digest.Ok()) {
    Abort();
    return Status(digest.GetError());
  }

  // 分块级哈希校验：客户端通过 X-Chunk-Sha256 声明，服务端以自己算的为准
  if (expected_chunk_hash_.has_value() && !expected_chunk_hash_->empty() &&
      *expected_chunk_hash_ != digest.Value()) {
    Abort();
    return Status(Error{"CHUNK_INVALID", "分块哈希与客户端声明不一致"});
  }

  auto storage_key = MakeChunkKey(upload_id_, chunk_index_);
  if (!storage_key) {
    Abort();
    return Status(storage_key.GetError());
  }

  UploadChunk chunk;
  chunk.upload_session_id = upload_id_;
  chunk.chunk_index = chunk_index_;
  chunk.size_bytes = bytes_written_;
  chunk.content_hash = digest.Value();
  chunk.storage_key = storage_key.Value();

  auto upserted = sessions_.UpsertChunk(chunk);
  if (!upserted) {
    Abort();
    return Status(upserted.GetError());
  }
  if (upserted.Value() == ChunkUpsertResult::kConflict) {
    // 同一索引已经存过**不同内容**：必须报冲突，不能悄悄覆盖
    Abort();
    return Status(Error{"CHUNK_CONFLICT", "该分块已存在且内容不同"});
  }

  finished_ = true;
  return Status::Success();
}

void ChunkUploadSession::Abort() {
  if (aborted_ || finished_) {
    return;
  }
  aborted_ = true;
  if (writer_ != nullptr) {
    (void)writer_->Finish();
    (void)storage_.Delete(writer_->temp_key());
  }
}

// ---- UploadService ----------------------------------------------------------

UploadService::UploadService(UploadSessionRepository& sessions, FileRepository& files,
                             FolderRepository& folders, UserRepository& users,
                             ObjectStorage& storage, const ResourcePermissionChecker& permissions,
                             UploadServiceOptions options)
    : sessions_(sessions),
      files_(files),
      folders_(folders),
      users_(users),
      storage_(storage),
      permissions_(permissions),
      options_(options) {}

Status UploadService::ScanObject(std::string_view storage_key, std::string_view content_hash) {
  if (!options_.scanner) {
    // 未配置扫描器：与 NoopFileScanner 等价，不阻断上传
    return Status::Success();
  }
  auto scanned = options_.scanner->Scan(storage_key, content_hash);
  if (!scanned) {
    return Status(scanned.GetError());
  }
  if (scanned.Value().verdict == ScanVerdict::kInfected) {
    spdlog::warn("上传内容未通过安全扫描 | scanner={} | key={}", options_.scanner->name(),
                 storage_key);
    return Status(Error{"FILE_REJECTED", "上传内容未通过安全扫描"});
  }
  // kClean 与 kSkipped 都放行：kSkipped 表示本期没有扫描能力（见 file_scanner.h）
  return Status::Success();
}

Status UploadService::CheckQuota(std::string_view user_id, std::uint64_t needed_bytes) {
  auto user = users_.FindById(user_id);
  if (!user) {
    return Status(user.GetError());
  }
  if (!user.Value().has_value()) {
    return Status(Error{"RESOURCE_NOT_FOUND", "用户不存在"});
  }
  const User& record = *user.Value();
  const std::uint64_t used = record.used_storage_bytes;
  const std::uint64_t quota = record.storage_quota_bytes;
  // 预检查用的是客户端声明的大小；权威判定必须用服务端算出的真实字节数
  // （FileRepository::CreateWithFirstVersion 在同一事务里再查一次并加锁）
  if (needed_bytes > quota || used > quota - needed_bytes) {
    return Status(Error{"QUOTA_EXCEEDED", "存储空间不足（已用 " + std::to_string(used) +
                                              " / 配额 " + std::to_string(quota) +
                                              " 字节，本次需要 " + std::to_string(needed_bytes) +
                                              " 字节）"});
  }
  return Status::Success();
}

Result<UploadSession> UploadService::LoadOwned(const Actor& actor, std::string_view upload_id) {
  auto found = sessions_.FindById(upload_id);
  if (!found) {
    return found.GetError();
  }
  if (!found.Value().has_value()) {
    return UploadNotFound();
  }
  const UploadSession session = *found.Value();

  // 上传会话的所有者校验：别人的会话一律当作不存在
  if (session.user_id != actor.user_id && actor.role != UserRole::kAdmin) {
    return UploadNotFound();
  }
  return session;
}

Result<UploadStatusView> UploadService::CreateSession(const Actor& actor,
                                                      const CreateUploadRequest& request) {
  if (actor.user_id.empty()) {
    return Error{"UNAUTHENTICATED", "缺少用户标识"};
  }

  // 阶段七：给已有文件新增版本。此时目录与文件名都以**文件自身**为准，
  // 客户端传的 folder_id/file_name 一律忽略，避免把版本写到别处或改名。
  std::optional<File> target_file;
  if (request.target_file_id.has_value() && !request.target_file_id->empty()) {
    auto found = files_.FindById(*request.target_file_id);
    if (!found) {
      return found.GetError();
    }
    if (!found.Value().has_value() || !found.Value()->IsActive()) {
      return Error{"FILE_NOT_FOUND", "目标文件不存在"};
    }
    const File& candidate = *found.Value();
    auto allowed = permissions_.Can(
        actor.user_id, actor.role,
        ResourceDescriptor{candidate.owner_id, "file", candidate.id, candidate.folder_id},
        ResourceAction::kWrite);
    if (!allowed) {
      return allowed.GetError();
    }
    if (!allowed.Value()) {
      // 与「不存在」返回同样的错误，避免探测他人文件
      return Error{"FILE_NOT_FOUND", "目标文件不存在"};
    }
    target_file = candidate;
  }

  // 目标目录必须存在且可写
  const std::string folder_id =
      target_file.has_value() ? target_file->folder_id : request.folder_id;
  const std::string file_name =
      target_file.has_value() ? target_file->original_name : request.file_name;

  auto folder = folders_.FindById(folder_id);
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

  auto name = ValidateFileName(file_name);
  if (!name) {
    return name.GetError();
  }

  // total_size 可以是 0（空文件按一个空分块处理，见 ComputeTotalChunks）；
  // 「有没有传 size_bytes」由控制器判定，服务层不能把 0 当成缺失。
  if (request.total_size > options_.max_file_size_bytes) {
    return Error{"INVALID_ARGUMENT", "文件超过单文件大小上限（" +
                                         std::to_string(options_.max_file_size_bytes) + " 字节）"};
  }

  // 配额预检查（阶段六）：用声明的大小先拦一次，权威判定在完成时用服务端算出的字节数
  auto quota_ok = CheckQuota(target.owner_id, request.total_size);
  if (!quota_ok.Ok()) {
    return quota_ok.GetError();
  }

  // 磁盘空间预检查（阶段十二）：磁盘写满时写入必然失败，
  // 与其让用户传完几十 GB 再报错，不如在创建会话时就拒绝。
  // 留出可用空间下限，避免把磁盘吃到 0 影响数据库等其他组件。
  auto available = storage_.AvailableBytes();
  if (!available) {
    return available.GetError();
  }
  const std::uint64_t reserve = options_.min_free_disk_bytes;
  if (available.Value() < request.total_size || available.Value() - request.total_size < reserve) {
    return Error{"STORAGE_FULL", "服务器磁盘可用空间不足（可用 " +
                                     std::to_string(available.Value()) + " 字节，需要 " +
                                     std::to_string(request.total_size) + " 字节 + 保留 " +
                                     std::to_string(reserve) + " 字节）"};
  }

  const std::uint32_t chunk_size =
      request.chunk_size == 0 ? options_.default_chunk_size : request.chunk_size;
  auto chunk_size_ok = ValidateChunkSize(chunk_size);
  if (!chunk_size_ok.Ok()) {
    return chunk_size_ok.GetError();
  }

  if (request.sha256.has_value() && !request.sha256->empty()) {
    const std::string& hash = *request.sha256;
    const bool looks_like_sha256 =
        hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](unsigned char c) {
          return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        });
    if (!looks_like_sha256) {
      return Error{"INVALID_ARGUMENT", "sha256 必须是 64 位小写十六进制"};
    }
  }

  // 并发上限：进行中的会话数（含本进程之外创建的，因为数据在数据库里）
  auto active_global = sessions_.CountInProgressGlobal();
  if (!active_global) {
    return active_global.GetError();
  }
  if (active_global.Value() >= options_.max_concurrent_uploads) {
    return Error{"STORAGE_BUSY", "服务器上传任务已达上限，请稍后重试"};
  }
  auto active_user = sessions_.CountInProgressByUser(actor.user_id);
  if (!active_user) {
    return active_user.GetError();
  }
  if (active_user.Value() >= options_.max_concurrent_uploads_per_user) {
    return Error{"STORAGE_BUSY", "你的并发上传任务已达上限，请先完成或取消其他上传"};
  }

  // 同名冲突提前拦住：等传完几十 GB 才说重名太糟糕了。
  // 新增版本时文件名不变，不做这个检查。
  if (!target_file.has_value()) {
    auto duplicated = files_.ExistsWithName(target.id, name.Value());
    if (!duplicated) {
      return duplicated.GetError();
    }
    if (duplicated.Value()) {
      return Error{"RESOURCE_CONFLICT", "同一目录下已存在同名文件"};
    }
  }

  UploadSession session;
  session.id = GenerateUuidV4();
  // 会话归属于**上传者**：被授权的协作者也能续传（阶段九）。
  // 文件最终归属目录所有者，在 Complete 里按目录解析，不能用这里的值。
  session.user_id = actor.user_id;
  session.folder_id = target.id;
  session.file_name = name.Value();
  session.mime_type = SanitizeMimeType(request.mime_type);
  session.total_size = request.total_size;
  session.chunk_size = chunk_size;
  session.total_chunks = ComputeTotalChunks(request.total_size, chunk_size);
  if (request.sha256.has_value() && !request.sha256->empty()) {
    session.expected_hash = *request.sha256;
  }
  session.status = UploadStatus::kInitiated;
  if (target_file.has_value()) {
    session.target_file_id = target_file->id;
    session.expected_version_id = request.expected_version_id;
  }

  auto created = sessions_.Create(session, options_.session_ttl_seconds);
  if (!created) {
    return created.GetError();
  }

  spdlog::info("创建上传会话 | user={} | upload_id={} | name={} | size={} | chunks={}",
               actor.user_id, created.Value().id, created.Value().file_name,
               created.Value().total_size, created.Value().total_chunks);

  UploadStatusView view;
  view.session = created.Value();

  // 去重预检查（阶段六）：用户自己已经有同样内容时告诉客户端，可以跳过传分块。
  // 只查自己的文件——上传接口不能变成「猜系统里有没有某个文件」的探测工具。
  if (session.expected_hash.has_value()) {
    auto existing = files_.FindOwnedObjectByHash(target.owner_id, *session.expected_hash);
    if (!existing) {
      return existing.GetError();
    }
    if (existing.Value().has_value() && existing.Value()->size_bytes == session.total_size) {
      view.already_exists = true;
      view.existing_object = *existing.Value();
      spdlog::info("去重命中 | user={} | upload_id={} | sha256={} | 已有文件={}", actor.user_id,
                   created.Value().id, *session.expected_hash, existing.Value()->file_name);
    }
  }
  return view;
}

Result<UploadStatusView> UploadService::GetStatus(const Actor& actor, std::string_view upload_id) {
  auto session = LoadOwned(actor, upload_id);
  if (!session) {
    return session.GetError();
  }

  auto chunks = sessions_.ListChunkIndexes(session.Value().id);
  if (!chunks) {
    return chunks.GetError();
  }
  auto bytes = sessions_.SumChunkBytes(session.Value().id);
  if (!bytes) {
    return bytes.GetError();
  }

  UploadStatusView view;
  view.session = session.Value();
  view.completed_chunks = std::move(chunks.Value());
  view.received_bytes = bytes.Value();
  return view;
}

Result<ChunkStart> UploadService::BeginChunk(const Actor& actor, std::string_view upload_id,
                                             std::uint32_t chunk_index,
                                             std::string_view client_chunk_sha256) {
  auto session = LoadOwned(actor, upload_id);
  if (!session) {
    return session.GetError();
  }
  const UploadSession& current = session.Value();

  if (current.status == UploadStatus::kExpired) {
    return Error{"UPLOAD_EXPIRED", "上传会话已过期"};
  }
  if (current.status == UploadStatus::kAborted) {
    return Error{"UPLOAD_ABORTED", "上传已被取消"};
  }
  if (current.status == UploadStatus::kCompleted) {
    return Error{"RESOURCE_CONFLICT", "上传已完成"};
  }
  if (current.status == UploadStatus::kMerging || current.status == UploadStatus::kVerifying) {
    return Error{"RESOURCE_CONFLICT", "上传正在处理中"};
  }
  if (current.status == UploadStatus::kFailed) {
    return Error{"UPLOAD_ABORTED", "上传已失败，请重新发起"};
  }

  if (chunk_index >= current.total_chunks) {
    return Error{"CHUNK_INVALID",
                 "分块索引越界（应在 0 到 " + std::to_string(current.total_chunks - 1) + " 之间）"};
  }

  // 重复分块：客户端声明了哈希就能立刻判定是幂等成功还是内容冲突
  auto existing = sessions_.FindChunk(current.id, chunk_index);
  if (!existing) {
    return existing.GetError();
  }
  if (existing.Value().has_value()) {
    const UploadChunk& stored = *existing.Value();
    // 只认「记录在 + 文件也在」的分块；文件丢失时按未上传处理，重传即可自愈
    if (storage_.ChunkExists(current.id, chunk_index)) {
      if (!client_chunk_sha256.empty() && client_chunk_sha256 != stored.content_hash) {
        return Error{"CHUNK_CONFLICT", "该分块已存在且内容不同"};
      }
      ChunkStart start;
      start.already_present = true;
      return start;
    }
    spdlog::warn("分块记录存在但文件丢失，按未上传重新接收 | upload_id={} | index={}", current.id,
                 chunk_index);
  }

  std::optional<std::string> expected;
  if (!client_chunk_sha256.empty()) {
    expected = std::string(client_chunk_sha256);
  }

  ChunkStart start;
  start.session = std::make_shared<ChunkUploadSession>(storage_, sessions_, current.id, chunk_index,
                                                       expected, current.chunk_size);
  return start;
}

Result<File> UploadService::Complete(const Actor& actor, std::string_view upload_id) {
  auto session = LoadOwned(actor, upload_id);
  if (!session) {
    return session.GetError();
  }

  // 已经完成过：幂等返回同一个文件
  if (session.Value().status == UploadStatus::kCompleted) {
    if (!session.Value().file_id.has_value()) {
      return Error{"INTERNAL_ERROR", "上传已完成但缺少文件记录"};
    }
    auto file = files_.FindById(*session.Value().file_id);
    if (!file) {
      return file.GetError();
    }
    if (!file.Value().has_value()) {
      return Error{"INTERNAL_ERROR", "上传已完成但文件记录不存在"};
    }
    return *file.Value();
  }

  if (session.Value().status == UploadStatus::kMerging ||
      session.Value().status == UploadStatus::kVerifying) {
    return Error{"RESOURCE_CONFLICT", "上传正在处理中，请稍后查询状态"};
  }
  if (session.Value().status == UploadStatus::kExpired) {
    return Error{"UPLOAD_EXPIRED", "上传会话已过期"};
  }
  if (session.Value().status == UploadStatus::kAborted) {
    return Error{"UPLOAD_ABORTED", "上传已被取消"};
  }
  if (session.Value().status == UploadStatus::kFailed) {
    return Error{"UPLOAD_ABORTED", "上传已失败，请重新发起"};
  }

  const UploadSession current = session.Value();

  // 上传者可能是被授权的协作者（阶段九）：文件必须归属**目录所有者**，
  // 不能用会话的 user_id（那是上传者）。配额也记在目录所有者名下。
  auto target_folder = folders_.FindById(current.folder_id);
  if (!target_folder) {
    return target_folder.GetError();
  }
  if (!target_folder.Value().has_value()) {
    return Error{"FOLDER_NOT_FOUND", "目标文件夹不存在"};
  }
  const std::string resource_owner = target_folder.Value()->owner_id;

  // 失败收尾：临时对象、磁盘分块、分块记录一起清掉。
  // 只清数据库记录而留下磁盘分块会让失败的上传一直占着空间，
  // 直到后台清理任务（阶段十一）才有机会回收。
  const auto fail_upload = [&](const std::string& error_code) {
    (void)storage_.DeleteChunks(current.id);
    (void)sessions_.DeleteChunks(current.id);
    (void)sessions_.UpdateStatus(current.id, UploadStatus::kFailed, error_code);
  };

  // 抢占：只有抢到的请求继续处理，其余并发请求会看到 MERGING。
  // 去重快捷路径与正常合并共用同一次抢占，保证并发 complete 只生成一个文件。
  const auto claim = [&]() -> Result<bool> { return sessions_.ClaimForMerge(current.id); };
  const auto resolve_claimed = [&]() -> Result<File> {
    auto reread = LoadOwned(actor, upload_id);
    if (reread && reread.Value().status == UploadStatus::kCompleted &&
        reread.Value().file_id.has_value()) {
      auto file = files_.FindById(*reread.Value().file_id);
      if (file && file.Value().has_value()) {
        return *file.Value();
      }
    }
    return Error{"RESOURCE_CONFLICT", "上传正在被另一个请求处理"};
  };

  // 分块数量与字节数（去重路径也需要知道「一个分块都没传」）
  auto chunk_count = sessions_.CountChunks(current.id);
  if (!chunk_count) {
    return chunk_count.GetError();
  }
  auto chunk_bytes = sessions_.SumChunkBytes(current.id);
  if (!chunk_bytes) {
    return chunk_bytes.GetError();
  }

  // 建文件或加版本：
  //   - 目标文件为空 → 新建文件记录 + 首个版本（阶段四/六）
  //   - 目标文件非空 → 给已有文件追加一个新版本（阶段七），版本号只增不减
  // 两条路径都带配额检查，且都在同一个事务里完成。
  const auto persist_content = [&](std::string_view content_hash, std::string_view storage_key,
                                   std::uint64_t size_bytes) -> Result<File> {
    // 安全扫描（阶段十二第 17 条）：本期是 NoopFileScanner，明确返回 skipped；
    // 接入真实引擎后这里会拦住带毒内容，对象也不会被任何文件引用
    auto scanned = ScanObject(storage_key, content_hash);
    if (!scanned.Ok()) {
      return scanned.GetError();
    }
    if (current.target_file_id.has_value()) {
      // 内容与当前版本一致时按幂等处理：不追加一个一模一样的版本
      auto existing_version = files_.FindCurrentVersion(*current.target_file_id);
      if (!existing_version) {
        return existing_version.GetError();
      }
      if (existing_version.Value().has_value() &&
          existing_version.Value()->content_hash == content_hash &&
          existing_version.Value()->size_bytes == size_bytes) {
        auto file = files_.FindById(*current.target_file_id);
        if (!file) {
          return file.GetError();
        }
        if (!file.Value().has_value()) {
          return Error{"FILE_NOT_FOUND", "目标文件不存在"};
        }
        return *file.Value();
      }

      FileVersion version;
      version.id = GenerateUuidV4();
      version.file_id = *current.target_file_id;
      version.content_hash = std::string(content_hash);
      version.size_bytes = size_bytes;
      version.storage_key = std::string(storage_key);
      // created_by 记的是真正上传的人（可能是协作者）
      version.created_by = actor.user_id;
      version.is_current = true;
      return files_.AddVersion(*current.target_file_id, version, current.expected_version_id);
    }

    File file = MakeFileRecord(resource_owner, current.folder_id, current.file_name,
                               current.mime_type, content_hash, storage_key, size_bytes);
    FileVersion version;
    version.id = GenerateUuidV4();
    version.file_id = file.id;
    version.version_number = 1;
    version.content_hash = std::string(content_hash);
    version.size_bytes = size_bytes;
    version.storage_key = std::string(storage_key);
    version.created_by = actor.user_id;
    version.is_current = true;
    return files_.CreateWithFirstVersion(file, version);
  };

  // ---- 去重快捷路径（阶段六）--------------------------------------------------
  //
  // 客户端声明了 SHA-256、且一个分块都没传：如果该用户自己已经有同样内容、
  // 大小也一致的文件，就直接引用已有对象建文件/加版本，不必重复上传。
  // 判定依据是**服务端数据库里的对象**（哈希与大小都是服务端记录的），
  // 不是客户端声明；客户端谎报哈希最多得到一个「自己已有文件」的副本。
  if (current.expected_hash.has_value() && chunk_count.Value() == 0) {
    auto existing = files_.FindOwnedObjectByHash(current.user_id, *current.expected_hash);
    if (!existing) {
      return existing.GetError();
    }
    if (existing.Value().has_value() && existing.Value()->size_bytes == current.total_size) {
      auto claimed = claim();
      if (!claimed) {
        return claimed.GetError();
      }
      if (!claimed.Value()) {
        return resolve_claimed();
      }

      const ExistingObject object = *existing.Value();
      auto created = persist_content(object.content_hash, object.storage_key, object.size_bytes);
      if (!created) {
        // 配额、版本冲突等都在这个事务里判定；失败按 FAILED 收尾
        fail_upload(created.GetError().code);
        return created.GetError();
      }
      auto attached = sessions_.AttachFileAndComplete(current.id, created.Value().id);
      if (!attached.Ok()) {
        spdlog::error("去重建文件成功但会话标记完成失败 | upload_id={} | file_id={}", current.id,
                      created.Value().id);
      }
      spdlog::info("去重直接完成 | user={} | upload_id={} | file_id={} | sha256={} | 引用已有对象",
                   current.user_id, current.id, created.Value().id, object.content_hash);
      return created.Value();
    }
  }

  // 分块必须齐：数量与总字节数都要对得上
  if (chunk_count.Value() != current.total_chunks || chunk_bytes.Value() != current.total_size) {
    return Error{"UPLOAD_INCOMPLETE", "分块不完整（已收到 " + std::to_string(chunk_count.Value()) +
                                          "/" + std::to_string(current.total_chunks) + " 块，" +
                                          std::to_string(chunk_bytes.Value()) + "/" +
                                          std::to_string(current.total_size) + " 字节）"};
  }

  auto claimed = claim();
  if (!claimed) {
    return claimed.GetError();
  }
  if (!claimed.Value()) {
    return resolve_claimed();
  }

  // 合并前复查磁盘空间：分块传完后磁盘可能已被写满，
  // 而且合并需要再占一份等量空间（分块与合并结果同时存在）。
  auto space_before_merge = storage_.AvailableBytes();
  if (!space_before_merge) {
    fail_upload(space_before_merge.GetError().code);
    return space_before_merge.GetError();
  }
  if (space_before_merge.Value() < current.total_size) {
    fail_upload("STORAGE_FULL");
    return Error{"STORAGE_FULL", "服务器磁盘可用空间不足，无法合并分块（可用 " +
                                     std::to_string(space_before_merge.Value()) + " 字节，需要 " +
                                     std::to_string(current.total_size) + " 字节）"};
  }

  // 合并到临时对象（不持数据库锁）
  auto merged = storage_.MergeChunksToTemp(current.id, current.total_chunks);
  if (!merged) {
    fail_upload(merged.GetError().code);
    return merged.GetError();
  }
  const std::string temp_key = merged.Value();

  // 取出物理路径算哈希；失败时要清理临时对象与分块
  auto temp_path = storage_.ResolveDownloadPath(temp_key);
  if (!temp_path) {
    (void)storage_.Delete(temp_key);
    fail_upload("STORAGE_UNAVAILABLE");
    return temp_path.GetError();
  }

  auto digest = Sha256OfFile(temp_path.Value());
  if (!digest) {
    (void)storage_.Delete(temp_key);
    fail_upload("STORAGE_UNAVAILABLE");
    return digest.GetError();
  }

  (void)sessions_.UpdateStatus(current.id, UploadStatus::kVerifying);

  // 服务端重算的大小必须与声明一致
  if (digest.Value().size_bytes != current.total_size) {
    (void)storage_.Delete(temp_key);
    fail_upload("HASH_MISMATCH");
    return Error{"HASH_MISMATCH", "服务端计算的字节数与声明不一致（" +
                                      std::to_string(digest.Value().size_bytes) + " vs " +
                                      std::to_string(current.total_size) + "）"};
  }

  // 校验客户端声明的 SHA-256：不匹配就不提交对象
  if (current.expected_hash.has_value() && *current.expected_hash != digest.Value().sha256_hex) {
    (void)storage_.Delete(temp_key);
    fail_upload("HASH_MISMATCH");
    spdlog::warn("上传哈希不匹配 | upload_id={} | expected={} | actual={}", current.id,
                 *current.expected_hash, digest.Value().sha256_hex);
    return Error{"HASH_MISMATCH", "文件内容与客户端声明的 SHA-256 不一致"};
  }

  auto final_key = MakeObjectKey(digest.Value().sha256_hex);
  if (!final_key) {
    (void)storage_.Delete(temp_key);
    fail_upload("INTERNAL_ERROR");
    return Error{"INTERNAL_ERROR", final_key.GetError().message};
  }

  auto committed = storage_.Commit(temp_key, final_key.Value());
  if (!committed.Ok()) {
    (void)storage_.Delete(temp_key);
    fail_upload(committed.GetError().code);
    return committed.GetError();
  }

  // 元数据落库（对象已经在最终位置，失败也只是留下一个无引用的对象）
  auto created =
      persist_content(digest.Value().sha256_hex, final_key.Value(), digest.Value().size_bytes);
  if (!created) {
    // 对象已提交到最终位置：内容寻址下可能被其他文件引用，这里只回收分块
    fail_upload(created.GetError().code);
    return created.GetError();
  }

  auto attached = sessions_.AttachFileAndComplete(current.id, created.Value().id);
  if (!attached.Ok()) {
    // 文件已经建好，把会话标成完成失败没有意义，如实记录
    spdlog::error("文件已创建但上传会话标记完成失败 | upload_id={} | file_id={}", current.id,
                  created.Value().id);
  }
  (void)storage_.DeleteChunks(current.id);

  spdlog::info("分块上传完成 | user={} | upload_id={} | file_id={} | bytes={} | sha256={}",
               current.user_id, current.id, created.Value().id, digest.Value().size_bytes,
               digest.Value().sha256_hex);
  return created.Value();
}

Status UploadService::Cancel(const Actor& actor, std::string_view upload_id) {
  auto session = LoadOwned(actor, upload_id);
  if (!session) {
    return Status(session.GetError());
  }

  if (session.Value().status == UploadStatus::kCompleted) {
    return Status(Error{"RESOURCE_CONFLICT", "上传已完成，无法取消"});
  }
  // 重复取消按幂等处理
  if (session.Value().status == UploadStatus::kAborted) {
    return Status::Success();
  }

  (void)sessions_.DeleteChunks(session.Value().id);
  (void)storage_.DeleteChunks(session.Value().id);
  auto updated = sessions_.UpdateStatus(session.Value().id, UploadStatus::kAborted);
  if (!updated.Ok()) {
    return updated;
  }

  spdlog::info("取消上传 | user={} | upload_id={}", actor.user_id, session.Value().id);
  return Status::Success();
}

}  // namespace lan_file_hub
