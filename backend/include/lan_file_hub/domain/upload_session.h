// 上传会话与分块领域模型。字段见 docs/data-model.md §UploadSession。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "lan_file_hub/common/result.h"

namespace lan_file_hub {

enum class UploadStatus {
  kInitiated,
  kUploading,
  kMerging,
  kVerifying,
  kCompleted,
  kFailed,
  kAborted,
  kExpired,
};

[[nodiscard]] std::string ToDbString(UploadStatus status);
[[nodiscard]] UploadStatus ParseUploadStatus(const std::string& value);
// 处于进行中（还能继续上传分块）的状态
[[nodiscard]] bool IsInProgress(UploadStatus status);
// 已经结束、不能再变更的状态
[[nodiscard]] bool IsTerminal(UploadStatus status);

struct UploadSession {
  std::string id;
  std::string user_id;
  std::string folder_id;
  std::string file_name;
  std::string mime_type;
  std::uint64_t total_size{0};
  std::uint32_t chunk_size{0};
  std::uint32_t total_chunks{0};
  std::optional<std::string> expected_hash;
  UploadStatus status{UploadStatus::kInitiated};
  std::optional<std::string> file_id;  // 完成后指向生成的文件
  // 阶段七：非空表示这次上传是给已有文件加新版本，完成时走 AddVersion 而不是新建文件
  std::optional<std::string> target_file_id;
  // 阶段七：客户端 If-Match 声明的当前版本，用于乐观并发控制
  std::optional<std::string> expected_version_id;
  std::optional<std::string> error_code;  // 失败原因
  std::string expires_at;
  std::string created_at;
  std::string updated_at;
};

struct UploadChunk {
  std::string upload_session_id;
  std::uint32_t chunk_index{0};
  std::uint64_t size_bytes{0};
  std::string content_hash;
  std::string storage_key;
  std::string created_at;
};

// 分块大小约束（与迁移里的 CHECK 一致，docs/storage.md）
inline constexpr std::uint32_t kMinChunkSize = 1U * 1024 * 1024;   // 1 MiB
inline constexpr std::uint32_t kMaxChunkSize = 64U * 1024 * 1024;  // 64 MiB

// 由总大小与分块大小算出总分块数（向上取整）；总大小为 0 时返回 1
[[nodiscard]] std::uint32_t ComputeTotalChunks(std::uint64_t total_size, std::uint32_t chunk_size);

// 校验客户端提交的分块是否在允许范围内
[[nodiscard]] Status ValidateChunkSize(std::uint32_t chunk_size);

}  // namespace lan_file_hub
