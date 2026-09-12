// 分块上传接口控制器。
//   POST   /api/v1/uploads                             创建上传会话
//   GET    /api/v1/uploads/{upload_id}                 查询状态（断点续传依据）
//   PUT    /api/v1/uploads/{upload_id}/chunks/{index}  上传分块（请求体流式写入）
//   POST   /api/v1/uploads/{upload_id}/complete        完成（服务端重算哈希）
//   DELETE /api/v1/uploads/{upload_id}                 取消
//
// 分块索引从 0 开始；重复上传同一分块幂等，内容不同返回 CHUNK_CONFLICT。
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace lan_file_hub {

class UploadController : public drogon::HttpController<UploadController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(UploadController::Create, "/api/v1/uploads", drogon::Post, "AuthFilter");
  ADD_METHOD_TO(UploadController::Status, "/api/v1/uploads/{upload_id}", drogon::Get, "AuthFilter");
  ADD_METHOD_TO(UploadController::Chunk, "/api/v1/uploads/{upload_id}/chunks/{chunk_index}",
                drogon::Put, "AuthFilter");
  ADD_METHOD_TO(UploadController::Complete, "/api/v1/uploads/{upload_id}/complete", drogon::Post,
                "AuthFilter");
  ADD_METHOD_TO(UploadController::Cancel, "/api/v1/uploads/{upload_id}", drogon::Delete,
                "AuthFilter");
  METHOD_LIST_END

  void Create(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  void Status(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
              std::string upload_id);

  void Chunk(const drogon::HttpRequestPtr& req,
             std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string upload_id,
             std::string chunk_index);

  void Complete(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                std::string upload_id);

  void Cancel(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
              std::string upload_id);
};

}  // namespace lan_file_hub
