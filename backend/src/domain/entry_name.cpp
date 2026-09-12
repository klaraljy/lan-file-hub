#include "lan_file_hub/domain/entry_name.h"

#include <cctype>
#include <string>

namespace lan_file_hub {

Result<std::string> ValidateEntryName(std::string_view name, std::string_view kind) {
  const std::string prefix(kind);

  std::size_t begin = 0;
  std::size_t end = name.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(name[begin])) != 0)
    ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(name[end - 1])) != 0)
    --end;
  const std::string trimmed(name.substr(begin, end - begin));

  if (trimmed.empty()) {
    return Error{"INVALID_ARGUMENT", prefix + "名称不能为空"};
  }
  if (trimmed.size() > kMaxEntryNameLength) {
    return Error{"INVALID_ARGUMENT",
                 prefix + "名称过长（最多 " + std::to_string(kMaxEntryNameLength) + " 个字符）"};
  }
  if (trimmed == "." || trimmed == "..") {
    return Error{"INVALID_ARGUMENT", prefix + "名称不能是 . 或 .."};
  }
  for (const char raw : trimmed) {
    const auto c = static_cast<unsigned char>(raw);
    if (c == '/' || c == '\\') {
      return Error{"INVALID_ARGUMENT", prefix + "名称不能包含路径分隔符"};
    }
    if (c < 0x20 || c == 0x7F) {
      return Error{"INVALID_ARGUMENT", prefix + "名称不能包含控制字符"};
    }
  }
  return trimmed;
}

}  // namespace lan_file_hub
