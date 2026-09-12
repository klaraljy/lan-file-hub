// 分享链接接口控制器（阶段九）。
//   POST   /api/v1/resources/{resource_id}/shares   创建（明文 Token 只返回这一次）
//   GET    /api/v1/resources/{resource_id}/shares   列出（不含 Token）
//   DELETE /api/v1/shares/{share_id}                 撤销（幂等）
//   GET    /api/v1/public/shares/{token}             公开查看（无需登录）
//   GET    /api/v1/public/shares/{token}/download    公开下载（无需登录，支持 Range）
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace lan_file_hub {

class ShareController : public drogon::HttpController<ShareController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(ShareController::Create, "/api/v1/resources/{resource_id}/shares", drogon::Post,
                "AuthFilter");
  ADD_METHOD_TO(ShareController::List, "/api/v1/resources/{resource_id}/shares", drogon::Get,
                "AuthFilter");
  ADD_METHOD_TO(ShareController::Revoke, "/api/v1/shares/{share_id}", drogon::Delete, "AuthFilter");
  // 公开接口：不需要 AuthFilter，但服务端仍然完整校验分享链接本身
  ADD_METHOD_TO(ShareController::PublicDescribe, "/api/v1/public/shares/{token}", drogon::Get);
  ADD_METHOD_TO(ShareController::PublicDownload, "/api/v1/public/shares/{token}/download",
                drogon::Get);
  METHOD_LIST_END

  void Create(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
              std::string resource_id);

  void List(const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            std::string resource_id);

  void Revoke(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string share_id);

  void PublicDescribe(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      std::string token);

  void PublicDownload(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      std::string token);
};

}  // namespace lan_file_hub
