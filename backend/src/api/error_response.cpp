#include "lan_file_hub/api/error_response.h"

#include <utility>

#include "lan_file_hub/common/metrics.h"
#include "lan_file_hub/common/request_id.h"

namespace lan_file_hub {
namespace {

drogon::HttpResponsePtr JsonResponse(const nlohmann::json& payload, drogon::HttpStatusCode status) {
  auto response = drogon::HttpResponse::newHttpResponse();
  response->setStatusCode(status);
  response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
  response->setBody(payload.dump());
  return response;
}

}  // namespace

drogon::HttpStatusCode HttpStatusForErrorCode(std::string_view code) {
  if (code == "INVALID_ARGUMENT")
    return drogon::k400BadRequest;
  if (code == "UNAUTHENTICATED")
    return drogon::k401Unauthorized;
  if (code == "TOKEN_EXPIRED")
    return drogon::k401Unauthorized;
  if (code == "FORBIDDEN")
    return drogon::k403Forbidden;
  // 邀请制注册（2026-10-02）：关掉自助注册后缺邀请 → 403；邀请不可用 → 400
  if (code == "INVITE_REQUIRED")
    return drogon::k403Forbidden;
  if (code == "INVITE_INVALID")
    return drogon::k400BadRequest;
  if (code == "RESOURCE_NOT_FOUND")
    return drogon::k404NotFound;
  if (code == "FILE_NOT_FOUND")
    return drogon::k404NotFound;
  if (code == "FOLDER_NOT_FOUND")
    return drogon::k404NotFound;
  if (code == "UPLOAD_NOT_FOUND")
    return drogon::k404NotFound;
  if (code == "RESOURCE_CONFLICT")
    return drogon::k409Conflict;
  if (code == "FILE_VERSION_CONFLICT")
    return drogon::k409Conflict;
  if (code == "CHUNK_CONFLICT")
    return drogon::k409Conflict;
  if (code == "UPLOAD_ABORTED")
    return drogon::k409Conflict;
  if (code == "QUOTA_EXCEEDED")
    return drogon::k409Conflict;
  if (code == "UPLOAD_EXPIRED")
    return drogon::k410Gone;
  if (code == "UPLOAD_INCOMPLETE")
    return drogon::k422UnprocessableEntity;
  if (code == "HASH_MISMATCH")
    return drogon::k422UnprocessableEntity;
  if (code == "CHUNK_INVALID")
    return drogon::k422UnprocessableEntity;
  if (code == "FILE_REJECTED")
    return drogon::k422UnprocessableEntity;
  if (code == "PAYLOAD_TOO_LARGE")
    return drogon::k413RequestEntityTooLarge;
  if (code == "RATE_LIMITED")
    return drogon::k429TooManyRequests;
  if (code == "STORAGE_FULL")
    return drogon::k507InsufficientStorage;
  if (code == "STORAGE_UNAVAILABLE")
    return drogon::k503ServiceUnavailable;
  if (code == "STORAGE_BUSY")
    return drogon::k503ServiceUnavailable;
  return drogon::k500InternalServerError;
}

drogon::HttpResponsePtr MakeErrorResponse(const Error& error, std::string_view request_id) {
  // 每个失败响应都按错误码计数（错误码取自 docs/api.md 的封闭表，基数有界）
  Metrics().IncrementCounter("lan_file_hub_errors_total", "按错误码统计的失败响应数",
                             {{"code", error.code}});
  nlohmann::json payload;
  nlohmann::json details = nlohmann::json::object();
  for (const auto& [key, value] : error.details) {
    details[key] = value;
  }
  payload["error"] = {
      {"code", error.code},
      {"message", error.message},
      {"details", std::move(details)},
  };
  payload["request_id"] = std::string(request_id);
  // detail 只进日志，不进响应体：避免把内部信息（SQL、路径、连接串）暴露给调用方
  return JsonResponse(payload, HttpStatusForErrorCode(error.code));
}

drogon::HttpResponsePtr MakeSuccessResponse(const nlohmann::json& data,
                                            std::string_view request_id) {
  nlohmann::json payload;
  payload["data"] = data;
  payload["request_id"] = std::string(request_id);
  return JsonResponse(payload, drogon::k200OK);
}

}  // namespace lan_file_hub
