#include "lan_file_hub/common/request_id.h"

#include <cctype>

#include "lan_file_hub/common/uuid.h"

namespace lan_file_hub {
namespace {

std::string& ThreadLocalRequestId() {
  static thread_local std::string request_id;
  return request_id;
}

bool IsAllowedChar(unsigned char c) {
  if (c >= '0' && c <= '9') {
    return true;
  }
  if (c >= 'a' && c <= 'z') {
    return true;
  }
  if (c >= 'A' && c <= 'Z') {
    return true;
  }
  return c == '.' || c == '_' || c == '-';
}

}  // namespace

bool IsValidRequestId(std::string_view candidate) {
  if (candidate.empty() || candidate.size() > kMaxRequestIdLength) {
    return false;
  }
  for (const char raw : candidate) {
    const auto c = static_cast<unsigned char>(raw);
    if (c > 0x7F || !IsAllowedChar(c)) {
      return false;
    }
  }
  return true;
}

std::string ResolveRequestId(std::string_view incoming) {
  if (IsValidRequestId(incoming)) {
    return std::string(incoming);
  }
  return GenerateUuidV4();
}

const std::string& CurrentRequestId() {
  return ThreadLocalRequestId();
}

void SetCurrentRequestId(std::string request_id) {
  ThreadLocalRequestId() = std::move(request_id);
}

void ClearCurrentRequestId() {
  ThreadLocalRequestId().clear();
}

}  // namespace lan_file_hub
