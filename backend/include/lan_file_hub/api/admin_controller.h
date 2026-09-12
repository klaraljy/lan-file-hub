// 管理员接口（阶段十三第 13～17 条）。全部走认证过滤器，权限判定在 AdminService 里。
// 响应绝不包含 password_hash、Token 或敏感配置。
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace lan_file_hub {

class AdminController : public drogon::HttpController<AdminController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(AdminController::ListUsers, "/api/v1/admin/users", drogon::Get, "AuthFilter");
  ADD_METHOD_TO(AdminController::SetUserStatus, "/api/v1/admin/users/{user_id}/status",
                drogon::Patch, "AuthFilter");
  ADD_METHOD_TO(AdminController::SetUserQuota, "/api/v1/admin/users/{user_id}/quota", drogon::Patch,
                "AuthFilter");
  ADD_METHOD_TO(AdminController::StorageStats, "/api/v1/admin/storage", drogon::Get, "AuthFilter");
  ADD_METHOD_TO(AdminController::QueryAuditLogs, "/api/v1/admin/audit-logs", drogon::Get,
                "AuthFilter");
  METHOD_LIST_END

  static void ListUsers(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  static void SetUserStatus(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            std::string user_id);

  static void SetUserQuota(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           std::string user_id);

  static void StorageStats(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  static void QueryAuditLogs(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

}  // namespace lan_file_hub
