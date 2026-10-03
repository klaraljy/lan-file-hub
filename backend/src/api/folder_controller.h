// 文件夹接口控制器。
//   GET    /api/v1/folders/root                  当前用户根目录
//   GET    /api/v1/folders/{folder_id}           文件夹详情
//   GET    /api/v1/folders/{folder_id}/children  子目录列表（分页）
//   GET    /api/v1/folders/{folder_id}/files     该目录**含子目录**的全部文件（供"一同下载"）
//   POST   /api/v1/folders                       新建文件夹
//   PATCH  /api/v1/folders/{folder_id}           重命名或移动
//   DELETE /api/v1/folders/{folder_id}           软删除（含子树）
// 全部需要认证；Controller 只做参数解析与响应组装（docs/architecture.md §7.1）。
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace lan_file_hub {

class FolderController : public drogon::HttpController<FolderController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(FolderController::Root, "/api/v1/folders/root", drogon::Get, "AuthFilter");
  ADD_METHOD_TO(FolderController::Children, "/api/v1/folders/{folder_id}/children", drogon::Get,
                "AuthFilter");
  ADD_METHOD_TO(FolderController::Files, "/api/v1/folders/{folder_id}/files", drogon::Get,
                "AuthFilter");
  ADD_METHOD_TO(FolderController::Path, "/api/v1/folders/{folder_id}/path", drogon::Get,
                "AuthFilter");
  ADD_METHOD_TO(FolderController::Detail, "/api/v1/folders/{folder_id}", drogon::Get, "AuthFilter");
  ADD_METHOD_TO(FolderController::Create, "/api/v1/folders", drogon::Post, "AuthFilter");
  ADD_METHOD_TO(FolderController::Update, "/api/v1/folders/{folder_id}", drogon::Patch,
                "AuthFilter");
  ADD_METHOD_TO(FolderController::Remove, "/api/v1/folders/{folder_id}", drogon::Delete,
                "AuthFilter");
  METHOD_LIST_END

  void Root(const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  void Children(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                std::string folder_id);

  // 「一同下载」：目录里（含子目录）所有文件的扁平清单（只要 id 与路径，不下发内容）
  void Files(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
             std::string folder_id);

  void Path(const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string folder_id);

  void Detail(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
              std::string folder_id);

  void Create(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  void Update(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
              std::string folder_id);

  void Remove(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
              std::string folder_id);
};

}  // namespace lan_file_hub
