// 文件接口控制器。
//   POST /api/v1/files?folder_id=&name=     单文件上传（请求体流式写入）
//   GET  /api/v1/files?folder_id=           目录内文件列表（分页）
//   GET  /api/v1/files/{file_id}            元数据
//   GET  /api/v1/files/{file_id}/download   下载（支持 Range）
//   GET  /api/v1/files/{file_id}/versions           历史版本列表（阶段七）
//   GET  /api/v1/files/{file_id}/versions/{vid}/download  历史版本下载（支持 Range）
//   POST /api/v1/files/{file_id}/versions/{vid}/restore   恢复历史版本（阶段七）
//
// 上传必须流式：Drogon 的 RequestStream 让我们边收边写盘边算 SHA-256，
// 整个文件不会进内存（AGENTS.md §5 红线）。
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace lan_file_hub {

class FileController : public drogon::HttpController<FileController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(FileController::Upload, "/api/v1/files", drogon::Post, "AuthFilter");
  ADD_METHOD_TO(FileController::List, "/api/v1/files", drogon::Get, "AuthFilter");
  ADD_METHOD_TO(FileController::Metadata, "/api/v1/files/{file_id}", drogon::Get, "AuthFilter");
  ADD_METHOD_TO(FileController::Download, "/api/v1/files/{file_id}/download", drogon::Get,
                "AuthFilter");
  ADD_METHOD_TO(FileController::ListVersions, "/api/v1/files/{file_id}/versions", drogon::Get,
                "AuthFilter");
  ADD_METHOD_TO(FileController::DownloadVersion,
                "/api/v1/files/{file_id}/versions/{version_id}/download", drogon::Get,
                "AuthFilter");
  ADD_METHOD_TO(FileController::RestoreVersion,
                "/api/v1/files/{file_id}/versions/{version_id}/restore", drogon::Post,
                "AuthFilter");
  ADD_METHOD_TO(FileController::Delete, "/api/v1/files/{file_id}", drogon::Delete, "AuthFilter");
  METHOD_LIST_END

  void Upload(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  void List(const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  void Metadata(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                std::string file_id);

  void Download(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                std::string file_id);

  void ListVersions(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    std::string file_id);

  void DownloadVersion(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       std::string file_id, std::string version_id);

  void RestoreVersion(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      std::string file_id, std::string version_id);

  // 软删除（进入回收站）；恢复与永久删除在 /api/v1/trash 下
  void Delete(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string file_id);
};

}  // namespace lan_file_hub
