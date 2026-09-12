// 认证接口控制器。
//   POST /api/v1/auth/register   注册（匿名）
//   POST /api/v1/auth/login      登录（匿名）
//   POST /api/v1/auth/logout     登出（需认证）
//   GET  /api/v1/auth/me         当前用户（需认证）
// Controller 只做参数解析与响应组装，业务在 AuthService（docs/architecture.md §7.1）。
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace lan_file_hub {

class AuthController : public drogon::HttpController<AuthController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(AuthController::Register, "/api/v1/auth/register", drogon::Post);
  ADD_METHOD_TO(AuthController::Login, "/api/v1/auth/login", drogon::Post);
  ADD_METHOD_TO(AuthController::Logout, "/api/v1/auth/logout", drogon::Post, "AuthFilter");
  ADD_METHOD_TO(AuthController::Me, "/api/v1/auth/me", drogon::Get, "AuthFilter");
  METHOD_LIST_END

  void Register(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  void Login(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  void Logout(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  void Me(const drogon::HttpRequestPtr& req,
          std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

}  // namespace lan_file_hub
