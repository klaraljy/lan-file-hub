#include "lan_file_hub/api/json_body.h"

#include <string>

namespace lan_file_hub {

Status CheckJsonBodySize(const drogon::HttpRequestPtr& req, std::size_t max_bytes) {
  if (!req) {
    return Status(Error{"INVALID_ARGUMENT", "请求体不能为空"});
  }

  const auto declared = req->realContentLength();
  if (declared > max_bytes) {
    return Status(Error{
        "PAYLOAD_TOO_LARGE", "请求体过大",
        "Content-Length=" + std::to_string(declared) + " 超过上限 " + std::to_string(max_bytes)});
  }

  // chunked 编码时没有 Content-Length（Drogon 返回 0），用实际体长兜底
  const auto actual = req->getBody().size();
  if (actual > max_bytes) {
    return Status(Error{
        "PAYLOAD_TOO_LARGE", "请求体过大",
        "实际请求体 " + std::to_string(actual) + " 字节超过上限 " + std::to_string(max_bytes)});
  }

  return Status::Success();
}

Result<nlohmann::json> ParseJsonBody(const drogon::HttpRequestPtr& req, std::size_t max_bytes) {
  const auto size_check = CheckJsonBodySize(req, max_bytes);
  if (!size_check.Ok()) {
    return size_check.GetError();
  }

  const auto body = req->getBody();
  if (body.empty()) {
    return Error{"INVALID_ARGUMENT", "请求体不能为空"};
  }
  try {
    return nlohmann::json::parse(std::string(body));
  } catch (const std::exception&) {
    return Error{"INVALID_ARGUMENT", "请求体不是合法的 JSON"};
  }
}

}  // namespace lan_file_hub
