// 邀请接口（用户 2026-10-02：不是所有人都能注册，要通过管理员邀请）。
//
//   POST   /api/v1/admin/invites                建邀请（ADMIN）→ 返回**一次性**的 token 明文
//   GET    /api/v1/admin/invites                列出邀请（ADMIN，不含 token）
//   DELETE /api/v1/admin/invites/{invite_id}    撤销（ADMIN，幂等）
//   GET    /api/v1/public/invites/{token}       校验邀请（公开，注册页用）
//   GET    /api/v1/public/registration-policy   当前是否允许自助注册（公开，注册页用）
//
// 契约见 docs/api.md「邀请端点」。
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace lan_file_hub {

class InviteController : public drogon::HttpController<InviteController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(InviteController::Create, "/api/v1/admin/invites", drogon::Post, "AuthFilter");
  ADD_METHOD_TO(InviteController::List, "/api/v1/admin/invites", drogon::Get, "AuthFilter");
  ADD_METHOD_TO(InviteController::Revoke, "/api/v1/admin/invites/{invite_id}", drogon::Delete,
                "AuthFilter");
  ADD_METHOD_TO(InviteController::Describe, "/api/v1/public/invites/{token}", drogon::Get);
  ADD_METHOD_TO(InviteController::RegistrationPolicy, "/api/v1/public/registration-policy",
                drogon::Get);
  METHOD_LIST_END

  void Create(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback);
  void List(const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback);
  // ⚠️ Drogon 把路径参数 `{invite_id}` / `{token}` 作为**最后一个函数参数**传进来，
  //    不能用 req->getParameter() 取（取到的是空串 → 表现为"永远查不到"，实测踩到过）。
  void Revoke(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
              std::string invite_id);
  void Describe(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string token);
  void RegistrationPolicy(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

}  // namespace lan_file_hub
