// 每个请求的 request_id：生成、校验、线程内传递。
// 规则见 docs/architecture.md（可观测性）与 docs/phases/13-observability-and-admin.md：
// 支持客户端传入 X-Request-Id，但必须校验长度与格式。
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace lan_file_hub {

// 客户端传入的 X-Request-Id 允许的最大长度。
inline constexpr std::size_t kMaxRequestIdLength = 64;

// 校验格式：1..64 个字符，仅允许 [A-Za-z0-9._-]。
// 不接受空串、超长串和任何控制字符/空白/非 ASCII。
[[nodiscard]] bool IsValidRequestId(std::string_view candidate);

// 合法则原样返回，否则生成新的 UUID v4。
[[nodiscard]] std::string ResolveRequestId(std::string_view incoming);

// 当前线程正在处理的请求 ID。空串表示不在请求上下文中。
[[nodiscard]] const std::string& CurrentRequestId();

// 设置/清除当前线程的请求 ID（由 RequestIdFilter 调用）。
void SetCurrentRequestId(std::string request_id);
void ClearCurrentRequestId();

}  // namespace lan_file_hub
