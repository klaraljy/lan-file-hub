#include "api/file_controller.h"

#include <exception>
#include <string>
#include <utility>

#include <drogon/RequestStream.h>
#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "api/http_metrics.h"
#include "lan_file_hub/api/download_streamer.h"
#include "lan_file_hub/api/error_response.h"
#include "lan_file_hub/application/app_context.h"
#include "lan_file_hub/common/http_util.h"
#include "lan_file_hub/common/metrics.h"
#include "lan_file_hub/common/pagination.h"
#include "lan_file_hub/common/request_id.h"
#include "lan_file_hub/infrastructure/jwt_service.h"

namespace lan_file_hub {
namespace {

// 对外的文件表示。不暴露 storage_key（内部实现细节）。
nlohmann::json FileToJson(const File& file) {
  nlohmann::json json{
      {"id", file.id},
      {"owner_id", file.owner_id},
      {"folder_id", file.folder_id},
      {"name", file.original_name},
      {"mime_type", file.mime_type},
      {"size_bytes", file.size_bytes},
      {"content_hash", file.content_hash},
      {"status", ToDbString(file.status)},
      {"created_at", file.created_at},
      {"updated_at", file.updated_at},
  };
  json["current_version_id"] = file.current_version_id.has_value()
                                   ? nlohmann::json(*file.current_version_id)
                                   : nlohmann::json(nullptr);
  json["deleted_at"] =
      file.deleted_at.has_value() ? nlohmann::json(*file.deleted_at) : nlohmann::json(nullptr);
  return json;
}

Actor ActorFrom(const drogon::HttpRequestPtr& req) {
  Actor actor;
  actor.user_id = req->attributes()->get<std::string>("user_id");
  const std::string role = req->attributes()->get<std::string>("user_role");
  actor.role = (role == "ADMIN") ? UserRole::kAdmin : UserRole::kUser;
  return actor;
}

void RespondError(const std::function<void(const drogon::HttpResponsePtr&)>& callback,
                  const Error& error) {
  if (!error.detail.empty()) {
    spdlog::warn("文件请求失败 | code={} | detail={}", error.code, RedactWithContext(error.detail));
  }
  callback(MakeErrorResponse(error, CurrentRequestId()));
}

// 用「下载票据」访问时，票据必须与本次请求的文件（以及版本）一致——防止拿 A 的票据去下 B。
// 没有票据属性就是普通会话令牌访问，直接放行（鉴权已经由 AuthFilter 做过）。
bool TicketAllows(const drogon::HttpRequestPtr& req, const std::string& file_id,
                  const std::string& version_id, Error& error) {
  std::string ticket_file;
  std::string ticket_version;
  try {
    ticket_file = req->attributes()->get<std::string>("download_ticket_file_id");
    ticket_version = req->attributes()->get<std::string>("download_ticket_version_id");
  } catch (const std::exception&) {
    return true;  // 不是票据访问
  }

  if (ticket_file != file_id) {
    error = Error{"FORBIDDEN", "下载票据与文件不匹配"};
    return false;
  }
  if (!version_id.empty() && ticket_version != version_id) {
    error = Error{"FORBIDDEN", "下载票据与版本不匹配"};
    return false;
  }
  return true;
}

}  // namespace

void FileController::Upload(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  UploadRequest request;
  request.folder_id = req->getParameter("folder_id");
  request.file_name = req->getParameter("name");
  // 客户端声明的 MIME 不可信，会经 SanitizeMimeType 清洗后再入库
  request.mime_type = req->getHeader("Content-Type");

  if (request.folder_id.empty()) {
    RespondError(callback, Error{"INVALID_ARGUMENT", "缺少 folder_id"});
    return;
  }
  if (request.file_name.empty()) {
    RespondError(callback, Error{"INVALID_ARGUMENT", "缺少 name"});
    return;
  }

  auto session = context->files_service->BeginUpload(ActorFrom(req), request);
  if (!session) {
    RespondError(callback, session.GetError());
    return;
  }
  const std::shared_ptr<FileUploadSession> upload = session.Value();

  // Drogon 只对「超过 clientMaxMemoryBodySize」的请求体启用流式；
  // 小请求体已经被它缓冲在内存里。两条路径都要走，但大文件一定是流式的
  //（main.cpp 把内存上限设成 1 MiB），所以整个文件永远不会进内存。
  auto stream = drogon::internal::createRequestStream(req);
  if (!stream) {
    const auto body = req->getBody();
    const auto written = upload->Write(body.data(), body.size());
    if (!written.Ok()) {
      upload->Abort();
      RespondError(callback, written.GetError());
      return;
    }
    auto created = upload->Finish();
    if (!created) {
      RespondError(callback, created.GetError());
      return;
    }
    callback(MakeSuccessResponse(FileToJson(created.Value()), CurrentRequestId()));
    return;
  }

  stream->setStreamReader(drogon::RequestStreamReader::newReader(
      // 数据块回调：写盘 + 更新 SHA-256。失败就抛异常中断读取。
      [upload](const char* data, std::size_t length) {
        const auto status = upload->Write(data, length);
        if (!status.Ok()) {
          throw drogon::StreamError(drogon::StreamErrorCode::kBadRequest,
                                    status.GetError().message);
        }
      },
      // 结束回调：正常收完就提交，出错就丢弃临时对象
      [upload, callback](std::exception_ptr error) {
        if (error) {
          std::string reason = "上传中断";
          try {
            std::rethrow_exception(error);
          } catch (const drogon::StreamError& stream_error) {
            reason = stream_error.what();
          } catch (const std::exception& ex) {
            reason = ex.what();
          }
          upload->Abort();
          RespondError(callback, Error{"INVALID_ARGUMENT", reason});
          return;
        }

        auto created = upload->Finish();
        if (!created) {
          RespondError(callback, created.GetError());
          return;
        }
        callback(MakeSuccessResponse(FileToJson(created.Value()), CurrentRequestId()));
      }));
}

void FileController::List(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  const std::string folder_id = req->getParameter("folder_id");
  if (folder_id.empty()) {
    RespondError(callback, Error{"INVALID_ARGUMENT", "缺少 folder_id"});
    return;
  }

  auto pagination = ParsePagination(req->getParameter("page"), req->getParameter("page_size"),
                                    req->getParameter("sort_by"), req->getParameter("sort_order"));
  if (!pagination) {
    RespondError(callback, pagination.GetError());
    return;
  }

  auto listing =
      context->files_service->ListInFolder(ActorFrom(req), folder_id, pagination.Value());
  if (!listing) {
    RespondError(callback, listing.GetError());
    return;
  }

  nlohmann::json items = nlohmann::json::array();
  for (const auto& file : listing.Value().files) {
    items.push_back(FileToJson(file));
  }
  callback(MakeSuccessResponse(
      nlohmann::json{
          {"items", std::move(items)},
          {"total", listing.Value().total},
          {"page", listing.Value().page},
          {"page_size", listing.Value().page_size},
      },
      CurrentRequestId()));
}

void FileController::Metadata(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              std::string file_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }
  auto file = context->files_service->GetMetadata(ActorFrom(req), file_id);
  if (!file) {
    RespondError(callback, file.GetError());
    return;
  }
  callback(MakeSuccessResponse(FileToJson(file.Value()), CurrentRequestId()));
}

void FileController::Download(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              std::string file_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto download = context->files_service->PrepareDownload(ActorFrom(req), file_id);
  if (!download) {
    Metrics().IncrementCounter(
        "lan_file_hub_downloads_total", "下载结果计数",
        {{"result", "failed"}, {"reason", DownloadFailureReason(download.GetError().code)}});
    RespondError(callback, download.GetError());
    return;
  }

  // 票据访问：票据里写死的文件必须与本次请求一致（权威校验）
  Error ticket_error;
  if (!TicketAllows(req, file_id, "", ticket_error)) {
    RespondError(callback, ticket_error);
    return;
  }

  const File& file = download.Value().file;
  const std::string& path = download.Value().absolute_path;
  const std::string range_header = req->getHeader("Range");

  auto range = ParseByteRange(range_header, file.size_bytes);
  if (!range) {
    // 416：Range 无法满足，按规范必须带上 Content-Range: bytes */<size>
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k416RequestedRangeNotSatisfiable);
    response->addHeader("Content-Range", "bytes */" + std::to_string(file.size_bytes));
    callback(response);
    return;
  }

  // 限速下载（阶段十二第 4 条）：配置了速率才走异步 chunked 流 + 令牌桶，
  // 默认（DOWNLOAD_RATE_LIMIT=0）继续走零拷贝路径，保留 Content-Length 与 Range 语义。
  const auto limiter = context->download_limiter;
  const std::string rate_key = "download|" + req->attributes()->get<std::string>("user_id");
  drogon::HttpResponsePtr response;
  if (limiter && limiter->enabled()) {
    response = MakeThrottledFileResponse(limiter, rate_key, path, range.Value().start,
                                         range.Value().length, file.mime_type,
                                         req->getConnectionPtr(), file.storage_key);
    if (range_header.empty()) {
      response->setStatusCode(drogon::k200OK);
    } else {
      response->setStatusCode(drogon::k206PartialContent);
      if (range.Value().length > 0) {
        response->addHeader("Content-Range",
                            "bytes " + std::to_string(range.Value().start) + "-" +
                                std::to_string(range.Value().start + range.Value().length - 1) +
                                "/" + std::to_string(file.size_bytes));
      }
    }
  } else if (range_header.empty()) {
    // 完整下载：Drogon 会分块发送文件，不会整文件进内存
    response =
        drogon::HttpResponse::newFileResponse(path, "", drogon::CT_CUSTOM, file.mime_type, req);
    response->setStatusCode(drogon::k200OK);
  } else {
    // 部分内容：setContentRange=true 会由 Drogon 补上 Content-Range
    response =
        drogon::HttpResponse::newFileResponse(path, range.Value().start, range.Value().length, true,
                                              "", drogon::CT_CUSTOM, file.mime_type, req);
    response->setStatusCode(drogon::k206PartialContent);
  }

  // 允许客户端后续发起 Range 请求
  response->addHeader("Accept-Ranges", "bytes");
  // 文件名可能含中文与引号，必须按 RFC 6266 编码
  response->addHeader("Content-Disposition", BuildContentDisposition(file.original_name));

  Metrics().IncrementCounter("lan_file_hub_downloads_total", "下载结果计数",
                             {{"result", "success"}, {"reason", "none"}});
  spdlog::info("文件下载 | actor={} | file_id={} | range={} | bytes={}",
               req->attributes()->get<std::string>("user_id"), file.id,
               range_header.empty() ? "full" : range_header, range.Value().length);
  callback(response);
}

// ---- 版本（阶段七）----------------------------------------------------------

namespace {

nlohmann::json VersionToJson(const FileVersion& version) {
  // 不暴露 storage_key：客户端只需要知道版本号、大小、哈希与时间
  return nlohmann::json{
      {"id", version.id},
      {"file_id", version.file_id},
      {"version_number", version.version_number},
      {"size_bytes", version.size_bytes},
      {"content_hash", version.content_hash},
      {"created_by", version.created_by},
      {"created_at", version.created_at},
      {"is_current", version.is_current},
  };
}

// 把 If-Match 头解析成版本 id。
// 支持裸 id 与带引号的 ETag 写法（`"<id>"`）；`*` 表示「不校验」，返回 nullopt。
std::optional<std::string> ParseIfMatch(const std::string& header) {
  std::string value = header;
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.erase(value.begin());
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  if (value.empty() || value == "*") {
    return std::nullopt;
  }
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value = value.substr(1, value.size() - 2);
  }
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

void FileController::ListVersions(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                  std::string file_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }
  auto versions = context->files_service->ListVersions(ActorFrom(req), file_id);
  if (!versions) {
    RespondError(callback, versions.GetError());
    return;
  }

  nlohmann::json items = nlohmann::json::array();
  for (const auto& version : versions.Value()) {
    items.push_back(VersionToJson(version));
  }
  callback(MakeSuccessResponse(
      nlohmann::json{{"items", std::move(items)}, {"total", versions.Value().size()}},
      CurrentRequestId()));
}

void FileController::DownloadVersion(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                     std::string file_id, std::string version_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto download =
      context->files_service->PrepareVersionDownload(ActorFrom(req), file_id, version_id);
  if (!download) {
    Metrics().IncrementCounter(
        "lan_file_hub_downloads_total", "下载结果计数",
        {{"result", "failed"}, {"reason", DownloadFailureReason(download.GetError().code)}});
    RespondError(callback, download.GetError());
    return;
  }

  // 票据访问：票据里写死的文件与版本都必须与本次请求一致（权威校验）
  Error ticket_error;
  if (!TicketAllows(req, file_id, version_id, ticket_error)) {
    RespondError(callback, ticket_error);
    return;
  }

  const File& file = download.Value().file;
  const FileVersion& version = download.Value().version;
  const std::string& path = download.Value().absolute_path;
  const std::string range_header = req->getHeader("Range");

  auto range = ParseByteRange(range_header, version.size_bytes);
  if (!range) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k416RequestedRangeNotSatisfiable);
    response->addHeader("Content-Range", "bytes */" + std::to_string(version.size_bytes));
    callback(response);
    return;
  }

  // 限速下载：与当前版本共用同一个"按用户"的令牌桶，避免用历史版本绕过限速
  const auto version_limiter = context->download_limiter;
  const std::string rate_key = "download|" + req->attributes()->get<std::string>("user_id");
  drogon::HttpResponsePtr response;
  if (version_limiter && version_limiter->enabled()) {
    response = MakeThrottledFileResponse(version_limiter, rate_key, path, range.Value().start,
                                         range.Value().length, file.mime_type,
                                         req->getConnectionPtr(), file.storage_key);
    if (range_header.empty()) {
      response->setStatusCode(drogon::k200OK);
    } else {
      response->setStatusCode(drogon::k206PartialContent);
      if (range.Value().length > 0) {
        response->addHeader("Content-Range",
                            "bytes " + std::to_string(range.Value().start) + "-" +
                                std::to_string(range.Value().start + range.Value().length - 1) +
                                "/" + std::to_string(version.size_bytes));
      }
    }
  } else if (range_header.empty()) {
    response =
        drogon::HttpResponse::newFileResponse(path, "", drogon::CT_CUSTOM, file.mime_type, req);
    response->setStatusCode(drogon::k200OK);
  } else {
    response =
        drogon::HttpResponse::newFileResponse(path, range.Value().start, range.Value().length, true,
                                              "", drogon::CT_CUSTOM, file.mime_type, req);
    response->setStatusCode(drogon::k206PartialContent);
  }

  response->addHeader("Accept-Ranges", "bytes");
  // 历史版本用 v<版本号> 作为文件名后缀，避免和当前版本混淆
  response->addHeader(
      "Content-Disposition",
      BuildContentDisposition(file.original_name + ".v" + std::to_string(version.version_number)));
  // 让客户端知道自己在下载哪个版本
  response->addHeader("X-File-Version", std::to_string(version.version_number));

  Metrics().IncrementCounter("lan_file_hub_downloads_total", "下载结果计数",
                             {{"result", "success"}, {"reason", "none"}});
  spdlog::info("历史版本下载 | actor={} | file_id={} | version={} | bytes={}",
               req->attributes()->get<std::string>("user_id"), file.id, version.version_number,
               range.Value().length);
  callback(response);
}

void FileController::RestoreVersion(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                    std::string file_id, std::string version_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  // 乐观并发：If-Match 声明的当前版本必须与服务端一致
  const auto expected = ParseIfMatch(req->getHeader("If-Match"));
  auto restored =
      context->files_service->RestoreVersion(ActorFrom(req), file_id, version_id, expected);
  if (!restored) {
    RespondError(callback, restored.GetError());
    return;
  }

  spdlog::info("恢复历史版本 | actor={} | file_id={} | version_id={} | current={}",
               req->attributes()->get<std::string>("user_id"), file_id, version_id,
               restored.Value().current_version_id.value_or(""));
  callback(MakeSuccessResponse(FileToJson(restored.Value()), CurrentRequestId()));
}

void FileController::Delete(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            std::string file_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  // 软删除：只改状态，磁盘对象不动，所以恢复是廉价的（阶段八）。
  // 永久删除在 DELETE /api/v1/trash/{id}/purge。
  auto status = context->trash_service->SoftDeleteFile(ActorFrom(req), file_id);
  if (!status.Ok()) {
    RespondError(callback, status.GetError());
    return;
  }

  auto file = context->files_service->GetMetadataIncludingTrashed(ActorFrom(req), file_id);
  nlohmann::json payload{{"id", file_id}, {"deleted", true}, {"async_purge", false}};
  if (file.Ok()) {
    payload["name"] = file.Value().original_name;
    payload["status"] = ToDbString(file.Value().status);
    payload["deleted_at"] = file.Value().deleted_at.has_value()
                                ? nlohmann::json(*file.Value().deleted_at)
                                : nlohmann::json(nullptr);
  }
  callback(MakeSuccessResponse(payload, CurrentRequestId()));
}

void FileController::CreateDownloadTicket(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string file_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  const Actor actor = ActorFrom(req);
  // 签发前先按正常权限检查一次：**票据不等于授权**，下载时还会再查一次
  auto download = context->files_service->PrepareDownload(actor, file_id);
  if (!download) {
    RespondError(callback, download.GetError());
    return;
  }

  auto ticket =
      context->jwt->IssueDownloadTicket(actor.user_id, file_id, "", kDownloadTicketTtlSeconds);
  if (!ticket) {
    RespondError(callback, ticket.GetError());
    return;
  }

  // 日志里不写票据本身（那是凭据）
  spdlog::info("签发下载票据 | user={} | file_id={} | ttl_seconds={}", actor.user_id, file_id,
               kDownloadTicketTtlSeconds);
  callback(MakeSuccessResponse(
      nlohmann::json{{"ticket", ticket.Value()},
                     {"url", "/api/v1/files/" + file_id + "/download?ticket=" + ticket.Value()},
                     {"expires_in", kDownloadTicketTtlSeconds}},
      CurrentRequestId()));
}

void FileController::CreateVersionDownloadTicket(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string file_id,
    std::string version_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  const Actor actor = ActorFrom(req);
  auto download = context->files_service->PrepareVersionDownload(actor, file_id, version_id);
  if (!download) {
    RespondError(callback, download.GetError());
    return;
  }

  auto ticket = context->jwt->IssueDownloadTicket(actor.user_id, file_id, version_id,
                                                  kDownloadTicketTtlSeconds);
  if (!ticket) {
    RespondError(callback, ticket.GetError());
    return;
  }

  spdlog::info("签发历史版本下载票据 | user={} | file_id={} | version_id={}", actor.user_id,
               file_id, version_id);
  callback(MakeSuccessResponse(
      nlohmann::json{{"ticket", ticket.Value()},
                     {"url", "/api/v1/files/" + file_id + "/versions/" + version_id +
                                 "/download?ticket=" + ticket.Value()},
                     {"expires_in", kDownloadTicketTtlSeconds}},
      CurrentRequestId()));
}

}  // namespace lan_file_hub
