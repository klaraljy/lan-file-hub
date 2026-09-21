// 用户名录接口：给"发送给某个人"提供按用户名/邮箱找人。
//   GET /api/v1/users/lookup?q=<关键词>
//
// 只返回 id 与 username；需要登录（AuthFilter），业务规则在 application/user_directory.h。
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace lan_file_hub {

class UserLookupController : public drogon::HttpController<UserLookupController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(UserLookupController::Lookup, "/api/v1/users/lookup", drogon::Get, "AuthFilter");
  METHOD_LIST_END

  void Lookup(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

}  // namespace lan_file_hub
