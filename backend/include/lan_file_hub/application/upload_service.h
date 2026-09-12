// 可恢复分块上传应用服务。
//
// 状态机（docs/data-model.md §UploadSession）：
//   INITIATED → UPLOADING → MERGING → VERIFYING → COMPLETED
//                                        ↘ FAILED / ABORTED / EXPIRED
//
// 几条硬要求（docs/phases/05-resumable-chunked-upload.md）：
//   - 会话与分块状态持久化在 PostgreSQL，服务重启后能续传
//   - 重复上传同一分块且内容一致 → 幂等成功；内容不同 → CHUNK_CONFLICT
//   - 完成时由**服务端重新计算**大小与 SHA-256，并校验客户端声明
//   - 同一个会话只能被一个请求完成（靠数据库 compare-and-set 抢占）
//   - 合并过程不持有数据库锁，也不把整个文件读进内存
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lan_file_hub/application/file_scanner.h"
#include "lan_file_hub/application/permission_checker.h"
#include "lan_file_hub/common/result.h"
#include "lan_file_hub/common/sha256.h"
#include "lan_file_hub/domain/file.h"
#include "lan_file_hub/domain/upload_session.h"
#include "lan_file_hub/infrastructure/file_repository.h"
#include "lan_file_hub/infrastructure/folder_repository.h"
#include "lan_file_hub/infrastructure/upload_session_repository.h"
#include "lan_file_hub/infrastructure/user_repository.h"
#include "lan_file_hub/storage/object_storage.h"

namespace lan_file_hub {

struct Actor;

struct UploadServiceOptions {
  std::uint32_t default_chunk_size{8U * 1024 * 1024};  // 8 MiB
  std::uint64_t max_file_size_bytes{10ULL * 1024 * 1024 * 1024};
  std::uint32_t session_ttl_seconds{24U * 60U * 60U};  // 24 小时
  std::uint32_t max_concurrent_uploads{16};            // 全局
  std::uint32_t max_concurrent_uploads_per_user{4};    // 单用户
  // 阶段十二：无论何时都要保留的磁盘可用空间（默认 1 GiB），
  // 避免把盘吃到 0 影响数据库与日志
  std::uint64_t min_free_disk_bytes{1ULL * 1024 * 1024 * 1024};
  // 阶段十二第 17 条：内容安全扫描。为空表示不扫描（等同 NoopFileScanner）
  std::shared_ptr<FileScanner> scanner;
};

struct CreateUploadRequest {
  std::string folder_id;
  std::string file_name;
  std::string mime_type;
  std::uint64_t total_size{0};
  std::uint32_t chunk_size{0};  // 0 表示用配置里的默认值
  std::optional<std::string> sha256;
  // 阶段七：给已有文件新增版本时指定目标文件；此时 folder_id/file_name 由服务端按文件补齐
  std::optional<std::string> target_file_id;
  // 阶段七：If-Match 声明的当前版本，完成时用于乐观并发校验
  std::optional<std::string> expected_version_id;
};

// 上传状态视图：客户端据此决定还差哪些分块
struct UploadStatusView {
  UploadSession session;
  std::vector<std::uint32_t> completed_chunks;
  std::uint64_t received_bytes{0};
  // 去重预检查命中（阶段六）：该用户已经有同样内容的文件，客户端可以不传分块直接 complete。
  // 只在本用户自己的文件里查，不回答「系统里有没有这个哈希」。
  bool already_exists{false};
  std::optional<ExistingObject> existing_object;
};

// 开始写一个分块的结果：要么「已存在，直接成功」，要么给出写入会话
struct ChunkStart {
  bool already_present{false};
  std::shared_ptr<class ChunkUploadSession> session;
};

// 单个分块的写入会话。控制器把请求体的数据块喂进来。
class ChunkUploadSession {
 public:
  ChunkUploadSession(ObjectStorage& storage, UploadSessionRepository& sessions,
                     std::string upload_id, std::uint32_t chunk_index,
                     std::optional<std::string> expected_chunk_hash, std::uint64_t max_chunk_bytes);
  ~ChunkUploadSession();

  ChunkUploadSession(const ChunkUploadSession&) = delete;
  ChunkUploadSession& operator=(const ChunkUploadSession&) = delete;

  [[nodiscard]] Status Write(const char* data, std::size_t length);
  // 写盘 → 校验分块哈希 → 落库（幂等）
  [[nodiscard]] Status Finish();
  void Abort();

  [[nodiscard]] std::uint64_t bytes_written() const { return bytes_written_; }

 private:
  ObjectStorage& storage_;
  UploadSessionRepository& sessions_;
  std::string upload_id_;
  std::uint32_t chunk_index_;
  std::optional<std::string> expected_chunk_hash_;
  std::uint64_t max_chunk_bytes_;
  std::unique_ptr<ObjectWriter> writer_;
  Sha256Hasher hasher_;
  std::uint64_t bytes_written_{0};
  bool finished_{false};
  bool aborted_{false};
};

class UploadService {
 public:
  UploadService(UploadSessionRepository& sessions, FileRepository& files, FolderRepository& folders,
                UserRepository& users, ObjectStorage& storage,
                const ResourcePermissionChecker& permissions, UploadServiceOptions options = {});

  [[nodiscard]] Result<UploadStatusView> CreateSession(const Actor& actor,
                                                       const CreateUploadRequest& request);

  [[nodiscard]] Result<UploadStatusView> GetStatus(const Actor& actor, std::string_view upload_id);

  // 开始写某个分块；chunk_index 越界、会话不可用、他人会话都会在这里被挡住
  [[nodiscard]] Result<ChunkStart> BeginChunk(const Actor& actor, std::string_view upload_id,
                                              std::uint32_t chunk_index,
                                              std::string_view client_chunk_sha256);

  [[nodiscard]] Result<File> Complete(const Actor& actor, std::string_view upload_id);

  [[nodiscard]] Status Cancel(const Actor& actor, std::string_view upload_id);

 private:
  // 读取会话并做所有权 + 状态检查
  [[nodiscard]] Result<UploadSession> LoadOwned(const Actor& actor, std::string_view upload_id);

  // 创建会话时的配额预检查。权威检查在 FileRepository 的事务里，
  // 这里只是尽早告诉用户，避免传完几十 GB 才失败。
  [[nodiscard]] Status CheckQuota(std::string_view user_id, std::uint64_t needed_bytes);

  // 阶段十二第 17 条：内容安全扫描。未配置扫描器时直接放行；
  // 判定为 kInfected 时返回 FILE_REJECTED，对象不会被任何文件引用（交给后台清理）。
  [[nodiscard]] Status ScanObject(std::string_view storage_key, std::string_view content_hash);

  UploadSessionRepository& sessions_;
  FileRepository& files_;
  FolderRepository& folders_;
  UserRepository& users_;
  ObjectStorage& storage_;
  const ResourcePermissionChecker& permissions_;
  UploadServiceOptions options_;
};

}  // namespace lan_file_hub
