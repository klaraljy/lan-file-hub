// 文件夹领域模型。字段与 docs/data-model.md §Folder 一一对应。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "lan_file_hub/common/result.h"

namespace lan_file_hub {

enum class FolderStatus { kActive, kTrashed, kPurged };

[[nodiscard]] std::string ToDbString(FolderStatus status);
[[nodiscard]] FolderStatus ParseFolderStatus(const std::string& value);

struct Folder {
  std::string id;                        // UUID
  std::string owner_id;                  // 归属用户
  std::optional<std::string> parent_id;  // 根目录为 nullopt
  std::string name;                      // 根目录为空串
  std::string path_key;                  // 物化路径，见 migrations/0002
  FolderStatus status{FolderStatus::kActive};
  std::string created_at;  // ISO8601 UTC
  std::string updated_at;
  std::optional<std::string> deleted_at;

  [[nodiscard]] bool IsRoot() const { return !parent_id.has_value(); }
  [[nodiscard]] bool IsActive() const { return status == FolderStatus::kActive; }
};

// 文件夹名称上限（与迁移里的 CHECK 约束一致）
inline constexpr std::size_t kMaxFolderNameLength = 255;

// 名称校验（docs/phases/03-folders.md 约束）：
//   - 不能为空或只有空白
//   - 最长 255
//   - 禁止 '/'、'\' 与控制字符
//   - 禁止 "." 与 ".."
//   - 前后空白会被裁剪后再校验（返回裁剪后的名字）
[[nodiscard]] Result<std::string> ValidateFolderName(std::string_view name);

}  // namespace lan_file_hub
