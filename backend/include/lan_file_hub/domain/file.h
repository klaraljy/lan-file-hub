// 文件领域模型。字段与 docs/data-model.md §File 一一对应。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "lan_file_hub/common/result.h"
#include "lan_file_hub/domain/entry_name.h"

namespace lan_file_hub {

enum class FileStatus { kActive, kTrashed, kPurged };

[[nodiscard]] std::string ToDbString(FileStatus status);
[[nodiscard]] FileStatus ParseFileStatus(const std::string& value);

struct File {
  std::string id;  // UUID
  std::string owner_id;
  std::string folder_id;
  std::optional<std::string> current_version_id;
  std::string original_name;  // 只作元数据，绝不参与物理路径拼接
  std::string mime_type;
  std::uint64_t size_bytes{0};
  std::string content_hash;  // SHA-256 十六进制
  std::string storage_key;   // objects/<前两位>/<hash>
  FileStatus status{FileStatus::kActive};
  std::string created_at;
  std::string updated_at;
  std::optional<std::string> deleted_at;

  [[nodiscard]] bool IsActive() const { return status == FileStatus::kActive; }
};

struct FileVersion {
  std::string id;
  std::string file_id;
  std::int32_t version_number{1};
  std::string content_hash;
  std::uint64_t size_bytes{0};
  std::string storage_key;
  std::string created_by;
  std::string created_at;
  bool is_current{false};
};

// 文件名校验：与文件夹同一套规则，只是文案是「文件」
[[nodiscard]] Result<std::string> ValidateFileName(std::string_view name);

// MIME 类型清洗：只接受形如 type/subtype 的 ASCII 值，否则回落到
// application/octet-stream。客户端提供的 MIME 不可信。
[[nodiscard]] std::string SanitizeMimeType(std::string_view mime_type);

}  // namespace lan_file_hub
