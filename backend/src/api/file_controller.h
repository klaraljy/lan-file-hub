// 文件接口控制器。
//   POST /api/v1/files?folder_id=&name=     单文件上传（请求体流式写入）
//   GET  /api/v1/files?folder_id=           目录内文件列表（分页）
//   GET  /api/v1/files/{file_id}            元数据
//   GET  /api/v1/files/{file_id}/download   下载（支持 Range）
//   GET  /api/v1/files/{file_id}/versions           历史版本列表（阶段七）
//   GET  /api/v1/files/{file_id}/versions/{vid}/download  历史版本下载（支持 Range）
//   POST /api/v1/files/{file_id}/versions/{vid}/restore   恢复历史版本（阶段七）
//   POST /api/v1/files/{file_id}/download-ticket          取一张 60 秒下载票据（大文件流式下载）
//   POST /api/v1/files/{file_id}/versions/{vid}/download-ticket  历史版本的下载票据
//
// 上传必须流式：Drogon 的 RequestStream 让我们边收边写盘边算 SHA-256，
// 整个文件不会进内存（AGENTS.md §5 红线）。
//
// 下载为什么要票据：登录态下载原来走 axios blob（**整个文件先进浏览器内存**），5 GB 必崩。
// 改成先取 60 秒票据、再把票据拼进 URL 让**浏览器自己**下载 → 流式落盘、内存与文件大小无关。
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace lan_file_hub {

class FileController : public drogon::HttpController<FileController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(FileController::Upload, "/api/v1/files", drogon::Post, "AuthFilter");
  ADD_METHOD_TO(FileController::List, "/api/v1/files", drogon::Get, "AuthFilter");
  // 「最近文件」（2026-10-03）：跨目录按更新时间列出自己的文件。
  // ⛔ 刻意用独立路径而不是 /api/v1/files/recent：后者会和 /api/v1/files/{file_id} 抢匹配。
  ADD_METHOD_TO(FileController::Recent, "/api/v1/recent-files", drogon::Get, "AuthFilter");
  // 搜索（2026-10-03）：按名字搜自己的文件与文件夹。
  // 同样不放在 /api/v1/files/... 下面，理由与上一条相同。
  ADD_METHOD_TO(FileController::Search, "/api/v1/search", drogon::Get, "AuthFilter");
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
  ADD_METHOD_TO(FileController::CreateDownloadTicket, "/api/v1/files/{file_id}/download-ticket",
                drogon::Post, "AuthFilter");
  ADD_METHOD_TO(FileController::CreateVersionDownloadTicket,
                "/api/v1/files/{file_id}/versions/{version_id}/download-ticket", drogon::Post,
                "AuthFilter");
  ADD_METHOD_TO(FileController::Delete, "/api/v1/files/{file_id}", drogon::Delete, "AuthFilter");
  METHOD_LIST_END

  void Upload(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  void List(const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  /** 最近文件（跨目录，按更新时间倒序；只列自己的有效文件） */
  void Recent(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  /** 搜索（按名字模糊匹配自己的文件与文件夹；`?q=` 必填） */
  void Search(const drogon::HttpRequestPtr& req,
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

  // 签发下载票据（先验权限，再签 60 秒票据；票据只对这一个文件/版本有效）
  void CreateDownloadTicket(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            std::string file_id);

  void CreateVersionDownloadTicket(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                   std::string file_id, std::string version_id);

  // 软删除（进入回收站）；恢复与永久删除在 /api/v1/trash 下
  void Delete(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string file_id);
};

}  // namespace lan_file_hub
