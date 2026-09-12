// 统一的 JSON 请求体读取：带大小上限，避免超大请求体被解析（阶段十二）。
#pragma once

#include <cstddef>

#include <drogon/HttpRequest.h>
#include <nlohmann/json.hpp>

#include "lan_file_hub/common/result.h"

namespace lan_file_hub {

// JSON 接口的请求体上限：这些接口只承载元数据（ID、名称、权限、分块大小），
// 正常请求都远小于此值。上限用来挡住"用超大 JSON 撑住内存与 CPU"的请求。
inline constexpr std::size_t kMaxJsonBodyBytes = 256 * 1024;

// 请求体超限时返回 PAYLOAD_TOO_LARGE（HTTP 413）。先看 Content-Length，
// 能在解析 JSON 之前就拒绝；再用实际体长兜底（chunked 编码没有 Content-Length）。
[[nodiscard]] Status CheckJsonBodySize(const drogon::HttpRequestPtr& req,
                                       std::size_t max_bytes = kMaxJsonBodyBytes);

// 统一的 JSON 请求体解析。失败时返回：
//   PAYLOAD_TOO_LARGE  请求体超过上限
//   INVALID_ARGUMENT   请求体为空或不是合法 JSON
[[nodiscard]] Result<nlohmann::json> ParseJsonBody(const drogon::HttpRequestPtr& req,
                                                   std::size_t max_bytes = kMaxJsonBodyBytes);

}  // namespace lan_file_hub
