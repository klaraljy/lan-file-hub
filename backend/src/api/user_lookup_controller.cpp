#include "api/user_lookup_controller.h"

#include <string>
#include <utility>

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "lan_file_hub/api/error_response.h"
#include "lan_file_hub/application/app_context.h"
#include "lan_file_hub/application/user_directory.h"
#include "lan_file_hub/common/request_id.h"

namespace lan_file_hub {

void UserLookupController::Lookup(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context) {
    callback(MakeErrorResponse(Error{"INTERNAL_ERROR", "服务未初始化"}, CurrentRequestId()));
    return;
  }

  const std::string keyword = req->getParameter("q");
  const std::string self_id = req->attributes()->get<std::string>("user_id");
  auto users = SearchUsersForSharing(*context->users, keyword, self_id);
  if (!users) {
    const Error error = users.GetError();
    // 参数问题不写告警日志（用户手滑打一个字很常见），其它错误要留痕
    if (error.code != "INVALID_ARGUMENT") {
      spdlog::warn("查找用户失败 | code={} | detail={}", error.code,
                   RedactWithContext(error.detail));
    }
    callback(MakeErrorResponse(error, CurrentRequestId()));
    return;
  }

  nlohmann::json items = nlohmann::json::array();
  for (const auto& user : users.Value()) {
    // 只回 id 与用户名：前端不需要、也不应该拿到邮箱等其它字段
    items.push_back(nlohmann::json{{"id", user.id}, {"username", user.username}});
  }
  callback(MakeSuccessResponse(nlohmann::json{{"items", std::move(items)}}, CurrentRequestId()));
}

}  // namespace lan_file_hub
