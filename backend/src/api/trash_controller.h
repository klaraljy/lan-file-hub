// 回收站接口控制器（阶段八）。
//   GET    /api/v1/trash                       回收站列表（分页、按类型筛选）
//   POST   /api/v1/trash/{resource_id}/restore 恢复（幂等；原目录不在则回到根目录）
//   DELETE /api/v1/trash/{resource_id}/purge   永久删除（对象交给后台清理任务）
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace lan_file_hub {

class TrashController : public drogon::HttpController<TrashController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(TrashController::List, "/api/v1/trash", drogon::Get, "AuthFilter");
  ADD_METHOD_TO(TrashController::Restore, "/api/v1/trash/{resource_id}/restore", drogon::Post,
                "AuthFilter");
  ADD_METHOD_TO(TrashController::Purge, "/api/v1/trash/{resource_id}/purge", drogon::Delete,
                "AuthFilter");
  METHOD_LIST_END

  void List(const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  void Restore(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               std::string resource_id);

  void Purge(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
             std::string resource_id);
};

}  // namespace lan_file_hub
