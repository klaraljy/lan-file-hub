// 权限与分享接口控制器（阶段九）。
//   GET    /api/v1/resources/{resource_id}/permissions
//   PUT    /api/v1/resources/{resource_id}/permissions/{user_id}
//   DELETE /api/v1/resources/{resource_id}/permissions/{user_id}
//   GET    /api/v1/resources/{resource_id}/effective-permission
//   POST   /api/v1/resources/{resource_id}/shares
//   GET    /api/v1/resources/{resource_id}/shares
//   DELETE /api/v1/shares/{share_id}
//
// resource_id 不带类型：服务端先按文件查，再按文件夹查（docs/api.md §权限端点）。
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace lan_file_hub {

class PermissionController : public drogon::HttpController<PermissionController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(PermissionController::List, "/api/v1/resources/{resource_id}/permissions",
                drogon::Get, "AuthFilter");
  ADD_METHOD_TO(PermissionController::Grant,
                "/api/v1/resources/{resource_id}/permissions/{user_id}", drogon::Put, "AuthFilter");
  ADD_METHOD_TO(PermissionController::Revoke,
                "/api/v1/resources/{resource_id}/permissions/{user_id}", drogon::Delete,
                "AuthFilter");
  ADD_METHOD_TO(PermissionController::Effective,
                "/api/v1/resources/{resource_id}/effective-permission", drogon::Get, "AuthFilter");
  ADD_METHOD_TO(PermissionController::SharedWithMe, "/api/v1/shared-with-me", drogon::Get,
                "AuthFilter");
  METHOD_LIST_END

  void List(const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            std::string resource_id);

  void Grant(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
             std::string resource_id, std::string user_id);

  void Revoke(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
              std::string resource_id, std::string user_id);

  void Effective(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                 std::string resource_id);

  // 「分享给我的」：列出直接授权给当前用户的资源
  void SharedWithMe(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

}  // namespace lan_file_hub
