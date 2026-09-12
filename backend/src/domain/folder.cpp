#include "lan_file_hub/domain/folder.h"

#include <stdexcept>

#include "lan_file_hub/domain/entry_name.h"

namespace lan_file_hub {

std::string ToDbString(FolderStatus status) {
  switch (status) {
    case FolderStatus::kTrashed:
      return "TRASHED";
    case FolderStatus::kPurged:
      return "PURGED";
    case FolderStatus::kActive:
    default:
      return "ACTIVE";
  }
}

FolderStatus ParseFolderStatus(const std::string& value) {
  if (value == "ACTIVE")
    return FolderStatus::kActive;
  if (value == "TRASHED")
    return FolderStatus::kTrashed;
  if (value == "PURGED")
    return FolderStatus::kPurged;
  throw std::invalid_argument("未知的文件夹状态: " + value);
}

Result<std::string> ValidateFolderName(std::string_view name) {
  // 具体规则在 ValidateEntryName 里，文件与文件夹共用同一套
  return ValidateEntryName(name, "文件夹");
}

}  // namespace lan_file_hub
