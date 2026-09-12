// 资源名称校验：文件夹与文件共用同一套规则，只是提示文案不同。
//
// 规则（docs/phases/03-folders.md、docs/phases/04-*.md）：
//   - 不能为空或只有空白
//   - 最长 255
//   - 禁止 '/'、'\' 与控制字符
//   - 禁止 "." 与 ".."
//   - 首尾空白会被裁剪后再校验
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "lan_file_hub/common/result.h"

namespace lan_file_hub {

inline constexpr std::size_t kMaxEntryNameLength = 255;

// kind 用于文案，例如 "文件夹" / "文件"。
[[nodiscard]] Result<std::string> ValidateEntryName(std::string_view name, std::string_view kind);

}  // namespace lan_file_hub
