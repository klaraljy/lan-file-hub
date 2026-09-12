#include "lan_file_hub/storage/object_storage.h"

#include <algorithm>
#include <cctype>

namespace lan_file_hub {
namespace {

bool IsHexLower(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

bool IsSafeKeyChar(unsigned char c) {
  if (c >= 'a' && c <= 'z')
    return true;
  if (c >= '0' && c <= '9')
    return true;
  return c == '/' || c == '.' || c == '_' || c == '-';
}

}  // namespace

Status ValidateObjectKey(std::string_view key) {
  if (key.empty()) {
    return Status(Error{"INVALID_ARGUMENT", "对象 Key 不能为空"});
  }
  if (key.size() > kMaxObjectKeyLength) {
    return Status(Error{"INVALID_ARGUMENT", "对象 Key 过长"});
  }
  if (key.front() == '/') {
    return Status(Error{"INVALID_ARGUMENT", "对象 Key 不能是绝对路径"});
  }
  // Windows 盘符形式
  if (key.size() >= 2 && key[1] == ':') {
    return Status(Error{"INVALID_ARGUMENT", "对象 Key 不能包含盘符"});
  }
  // 逐段检查：任何一段是 "." 或 ".." 都拒绝，杜绝目录穿越
  std::size_t start = 0;
  while (start <= key.size()) {
    const std::size_t end = key.find('/', start);
    const std::string_view segment =
        key.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    if (segment.empty() || segment == "." || segment == "..") {
      return Status(Error{"INVALID_ARGUMENT", "对象 Key 含非法路径段"});
    }
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  }
  for (const char raw : key) {
    const auto c = static_cast<unsigned char>(raw);
    if (c == '\\' || c < 0x20 || c > 0x7F || !IsSafeKeyChar(c)) {
      return Status(Error{"INVALID_ARGUMENT", "对象 Key 含非法字符"});
    }
  }
  return Status::Success();
}

Result<std::string> MakeObjectKey(std::string_view sha256_hex) {
  if (sha256_hex.size() != 64 || !std::all_of(sha256_hex.begin(), sha256_hex.end(), IsHexLower)) {
    return Error{"INTERNAL_ERROR", "内容哈希格式不正确，无法生成对象 Key"};
  }
  std::string key = "objects/";
  key.append(sha256_hex.substr(0, 2));
  key.push_back('/');
  key.append(sha256_hex);
  return key;
}

Result<std::string> MakeChunkKey(std::string_view upload_id, std::uint32_t chunk_index) {
  // upload_id 会直接进路径，必须先确认它只含安全字符
  if (upload_id.empty() || upload_id.size() > 64) {
    return Error{"INVALID_ARGUMENT", "上传会话标识不合法"};
  }
  for (const char raw : upload_id) {
    const auto c = static_cast<unsigned char>(raw);
    const bool safe = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!safe) {
      return Error{"INVALID_ARGUMENT", "上传会话标识含非法字符"};
    }
  }
  return "tmp/uploads/" + std::string(upload_id) + "/" + std::to_string(chunk_index);
}

}  // namespace lan_file_hub
