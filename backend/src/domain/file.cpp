#include "lan_file_hub/domain/file.h"

#include <stdexcept>

namespace lan_file_hub {

std::string ToDbString(FileStatus status) {
  switch (status) {
    case FileStatus::kTrashed:
      return "TRASHED";
    case FileStatus::kPurged:
      return "PURGED";
    case FileStatus::kActive:
    default:
      return "ACTIVE";
  }
}

FileStatus ParseFileStatus(const std::string& value) {
  if (value == "ACTIVE")
    return FileStatus::kActive;
  if (value == "TRASHED")
    return FileStatus::kTrashed;
  if (value == "PURGED")
    return FileStatus::kPurged;
  throw std::invalid_argument("未知的文件状态: " + value);
}

Result<std::string> ValidateFileName(std::string_view name) {
  return ValidateEntryName(name, "文件");
}

std::string SanitizeMimeType(std::string_view mime_type) {
  constexpr std::string_view kFallback = "application/octet-stream";
  if (mime_type.empty() || mime_type.size() > 128) {
    return std::string(kFallback);
  }
  // 只允许可见 ASCII，且必须恰好一个 '/'，避免头注入与奇怪值
  std::size_t slashes = 0;
  for (const char raw : mime_type) {
    const auto c = static_cast<unsigned char>(raw);
    if (c <= 0x20 || c >= 0x7F) {
      return std::string(kFallback);
    }
    if (raw == '/')
      ++slashes;
  }
  if (slashes != 1) {
    return std::string(kFallback);
  }
  return std::string(mime_type);
}

}  // namespace lan_file_hub
