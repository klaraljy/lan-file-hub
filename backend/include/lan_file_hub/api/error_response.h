// 统一错误响应：错误码 → HTTP 状态码 → JSON 响应体。
// 错误码取自 docs/api.md，禁止在此之外自造。
#pragma once

#include <string>
#include <string_view>

#include <drogon/HttpResponse.h>
#include <nlohmann/json.hpp>

#include "lan_file_hub/common/result.h"

namespace lan_file_hub {

// 统一错误响应体：{"error":{"code","message","details":{}},"request_id":"..."}
[[nodiscard]] drogon::HttpResponsePtr MakeErrorResponse(const Error& error,
                                                        std::string_view request_id);

// 统一成功响应体：{"data":{...},"request_id":"..."}
[[nodiscard]] drogon::HttpResponsePtr MakeSuccessResponse(const nlohmann::json& data,
                                                          std::string_view request_id);

// 错误码到 HTTP 状态码的映射，见 docs/api.md
[[nodiscard]] drogon::HttpStatusCode HttpStatusForErrorCode(std::string_view code);

}  // namespace lan_file_hub
