// 文件应用服务：单文件上传（流式）、元数据查询、下载准备、列表。
//
// 上传流程（docs/storage.md §分块上传协议 完成上传顺序的精简单文件版本）：
//   1. 开临时对象 → 边收边写边算 SHA-256（全程流式，不整文件进内存）
//   2. 写入结束 → 得到最终哈希与大小
//   3. 用哈希生成内容寻址的最终 key 并提交对象
//   4. 在事务里创建 file + file_version（version_number = 1）
// 第 3 步之后数据库失败时，对象留在盘上由后续阶段的清理任务处理，不会出现
// 「数据库指向不存在的对象」。
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "lan_file_hub/application/permission_checker.h"
#include "lan_file_hub/common/pagination.h"
#include "lan_file_hub/common/result.h"
#include "lan_file_hub/common/sha256.h"
#include "lan_file_hub/domain/file.h"
#include "lan_file_hub/infrastructure/file_repository.h"
#include "lan_file_hub/infrastructure/folder_repository.h"
#include "lan_file_hub/storage/object_storage.h"

namespace lan_file_hub {

struct Actor;  // 定义在 folder_service.h

struct FileServiceOptions {
  std::uint64_t max_file_size_bytes{10ULL * 1024 * 1024 * 1024};
  // 阶段十二：无论何时都要保留的磁盘可用空间（默认 1 GiB）
  std::uint64_t min_free_disk_bytes{1ULL * 1024 * 1024 * 1024};
};

struct UploadRequest {
  std::string folder_id;
  std::string file_name;
  std::string mime_type;
};

struct FileListing {
  std::vector<File> files;
  std::uint64_t total{0};
  int page{1};
  int page_size{kDefaultPageSize};
};

/** 最近文件（跨目录）：只列自己的有效文件，按更新时间倒序，并带上所在文件夹 */
struct RecentListing {
  std::vector<FileRepository::RecentFile> files;
  std::uint64_t total{0};
  int page{1};
  int page_size{kDefaultPageSize};
};

struct FileDownload {
  File file;
  std::string absolute_path;  // 交给 HTTP 层做流式响应
};

// 历史版本下载：除了文件本身还要带上版本信息，便于响应头里标注版本号
struct VersionDownload {
  File file;
  FileVersion version;
  std::string absolute_path;
};

// 一次流式上传。控制器持有 shared_ptr，在 Drogon 的数据回调里逐块喂入。
// 所有方法都不是线程安全的：同一个会话只会被一个 IO 线程驱动。
class FileUploadSession {
 public:
  FileUploadSession(ObjectStorage& storage, FileRepository& files,
                    std::unique_ptr<ObjectWriter> writer, std::string owner_id,
                    std::string folder_id, std::string file_name, std::string mime_type,
                    std::uint64_t max_bytes);
  ~FileUploadSession();

  FileUploadSession(const FileUploadSession&) = delete;
  FileUploadSession& operator=(const FileUploadSession&) = delete;

  // 追加一块数据；超过上限立即失败
  [[nodiscard]] Status Write(const char* data, std::size_t length);

  // 收完数据后调用：提交对象并落库。只能调用一次。
  [[nodiscard]] Result<File> Finish();

  // 客户端断开或出错时调用：丢弃临时对象，不落库
  void Abort();

  [[nodiscard]] std::uint64_t bytes_written() const { return bytes_written_; }
  [[nodiscard]] bool finished() const { return finished_; }

 private:
  ObjectStorage& storage_;
  FileRepository& files_;
  std::unique_ptr<ObjectWriter> writer_;
  Sha256Hasher hasher_;
  std::string owner_id_;
  std::string folder_id_;
  std::string file_name_;
  std::string mime_type_;
  std::uint64_t max_bytes_;
  std::uint64_t bytes_written_{0};
  bool finished_{false};
  bool aborted_{false};
};

class FileService {
 public:
  FileService(FileRepository& files, FolderRepository& folders, ObjectStorage& storage,
              const ResourcePermissionChecker& permissions, FileServiceOptions options = {});

  // 校验目标目录与文件名，开好临时对象；真正的数据由调用方喂入 session
  [[nodiscard]] Result<std::shared_ptr<FileUploadSession>> BeginUpload(
      const Actor& actor, const UploadRequest& request);

  [[nodiscard]] Result<FileListing> ListInFolder(const Actor& actor, std::string_view folder_id,
                                                 const Pagination& pagination);

  /** 最近文件（2026-10-03）：跨目录列出自己的有效文件，按更新时间倒序。
   *  只列**自己拥有**的文件 —— 别人分享给你的在「分享给我的」里，两处职责不混。 */
  [[nodiscard]] Result<RecentListing> ListRecent(const Actor& actor, const Pagination& pagination);

  [[nodiscard]] Result<File> GetMetadata(const Actor& actor, std::string_view file_id);

  // 删除后的文件仍然要能读元数据（前端回收站需要显示名字与删除时间）
  [[nodiscard]] Result<File> GetMetadataIncludingTrashed(const Actor& actor,
                                                         std::string_view file_id);

  // 权限检查 + 解析出物理路径，供 HTTP 层做带 Range 的流式响应
  [[nodiscard]] Result<FileDownload> PrepareDownload(const Actor& actor, std::string_view file_id);

  // ---- 版本与乐观并发（阶段七）----

  // 历史版本列表（新版本在前）。需要读权限。
  [[nodiscard]] Result<std::vector<FileVersion>> ListVersions(const Actor& actor,
                                                              std::string_view file_id);

  // 下载指定历史版本。需要读权限；对象丢失时返回明确错误并写日志。
  [[nodiscard]] Result<VersionDownload> PrepareVersionDownload(const Actor& actor,
                                                               std::string_view file_id,
                                                               std::string_view version_id);

  // 恢复到指定历史版本：**不覆盖任何对象**，而是把该版本的内容作为一个新版本追加，
  // 因此恢复动作本身也是可追溯、可再次回退的。
  //
  //   - expected_version_id 来自 If-Match，与当前版本不一致时返回 409 FILE_VERSION_CONFLICT
  //   - 当前版本内容与目标版本相同时按幂等处理：不新建版本，直接返回文件
  //   - 需要写权限；配额在 AddVersion 的事务里检查
  [[nodiscard]] Result<File> RestoreVersion(const Actor& actor, std::string_view file_id,
                                            std::string_view version_id,
                                            std::optional<std::string> expected_version_id);

 private:
  [[nodiscard]] Result<File> LoadAccessible(const Actor& actor, std::string_view file_id,
                                            ResourceAction action);

  FileRepository& files_;
  FolderRepository& folders_;
  ObjectStorage& storage_;
  const ResourcePermissionChecker& permissions_;
  FileServiceOptions options_;
};

// 供 FileUploadSession::Finish 使用：由哈希与大小组装 File / FileVersion
[[nodiscard]] File MakeFileRecord(std::string_view owner_id, std::string_view folder_id,
                                  std::string_view file_name, std::string_view mime_type,
                                  std::string_view content_hash, std::string_view storage_key,
                                  std::uint64_t size_bytes);

}  // namespace lan_file_hub
