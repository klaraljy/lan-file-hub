#include "api/upload_controller.h"

#include <exception>
#include <string>
#include <utility>

#include <drogon/RequestStream.h>
#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "lan_file_hub/api/error_response.h"
#include "lan_file_hub/api/json_body.h"
#include "lan_file_hub/application/app_context.h"
#include "lan_file_hub/common/metrics.h"
#include "lan_file_hub/common/rate_limiter.h"
#include "lan_file_hub/common/request_id.h"

namespace lan_file_hub {
namespace {

// docs/api.md 里约定的创建响应形状
nlohmann::json CreateResponse(const UploadStatusView& view) {
  nlohmann::json completed = nlohmann::json::array();
  for (const auto index : view.completed_chunks) {
    completed.push_back(index);
  }
  nlohmann::json json{
      {"upload_id", view.session.id},
      {"chunk_size", view.session.chunk_size},
      {"total_chunks", view.session.total_chunks},
      {"total_size", view.session.total_size},
      {"status", ToDbString(view.session.status)},
      // 去重预检查：该用户已有同样内容的文件，客户端可以跳过传分块直接 complete
      {"already_exists", view.already_exists},
      {"completed_chunks", std::move(completed)},
      {"expires_at", view.session.expires_at},
  };
  if (view.existing_object.has_value()) {
    // 只回用户自己的文件名，不回物理路径
    json["existing_file"] = {
        {"file_id", view.existing_object->file_id},
        {"name", view.existing_object->file_name},
        {"size_bytes", view.existing_object->size_bytes},
    };
  }
  return json;
}

nlohmann::json StatusResponse(const UploadStatusView& view) {
  nlohmann::json completed = nlohmann::json::array();
  for (const auto index : view.completed_chunks) {
    completed.push_back(index);
  }
  nlohmann::json json{
      {"upload_id", view.session.id},
      {"status", ToDbString(view.session.status)},
      {"file_name", view.session.file_name},
      {"total_size", view.session.total_size},
      {"chunk_size", view.session.chunk_size},
      {"total_chunks", view.session.total_chunks},
      {"completed_chunks", std::move(completed)},
      {"received_bytes", view.received_bytes},
      {"expires_at", view.session.expires_at},
  };
  if (view.session.error_code.has_value()) {
    json["error_code"] = *view.session.error_code;
  }
  if (view.session.file_id.has_value()) {
    json["file_id"] = *view.session.file_id;
  }
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
    spdlog::warn("上传请求失败 | code={} | detail={}", error.code, RedactWithContext(error.detail));
  }
  callback(MakeErrorResponse(error, CurrentRequestId()));
}

// 解析并校验路径参数里的分块索引
Result<std::uint32_t> ParseChunkIndex(const std::string& text) {
  if (text.empty() || text.size() > 10) {
    return Error{"CHUNK_INVALID", "分块索引不合法"};
  }
  std::uint32_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return Error{"CHUNK_INVALID", "分块索引必须是十进制非负整数"};
    }
    value = value * 10U + static_cast<std::uint32_t>(c - '0');
  }
  return value;
}

}  // namespace

void UploadController::Create(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto parsed_body = ParseJsonBody(req);
  if (!parsed_body) {
    RespondError(callback, parsed_body.GetError());
    return;
  }
  const nlohmann::json& body = parsed_body.Value();

  CreateUploadRequest request;
  if (body.contains("folder_id") && body["folder_id"].is_string()) {
    request.folder_id = body["folder_id"].get<std::string>();
  }
  if (body.contains("file_name") && body["file_name"].is_string()) {
    request.file_name = body["file_name"].get<std::string>();
  }
  if (body.contains("mime_type") && body["mime_type"].is_string()) {
    request.mime_type = body["mime_type"].get<std::string>();
  }
  if (body.contains("size_bytes") && body["size_bytes"].is_number_unsigned()) {
    request.total_size = body["size_bytes"].get<std::uint64_t>();
  } else if (body.contains("total_size") && body["total_size"].is_number_unsigned()) {
    request.total_size = body["total_size"].get<std::uint64_t>();
  }
  if (body.contains("chunk_size") && body["chunk_size"].is_number_unsigned()) {
    request.chunk_size = body["chunk_size"].get<std::uint32_t>();
  }
  if (body.contains("sha256") && body["sha256"].is_string()) {
    request.sha256 = body["sha256"].get<std::string>();
  }
  // 阶段七：给已有文件新增版本（客户端也可用 If-Match 头声明当前版本）
  if (body.contains("file_id") && body["file_id"].is_string()) {
    request.target_file_id = body["file_id"].get<std::string>();
  } else if (body.contains("target_file_id") && body["target_file_id"].is_string()) {
    request.target_file_id = body["target_file_id"].get<std::string>();
  }
  if (body.contains("expected_version_id") && body["expected_version_id"].is_string()) {
    request.expected_version_id = body["expected_version_id"].get<std::string>();
  }
  const std::string if_match = req->getHeader("If-Match");
  if (!if_match.empty() && if_match != "*") {
    std::string value = if_match;
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      value = value.substr(1, value.size() - 2);
    }
    request.expected_version_id = value;
  }

  const bool adding_version =
      request.target_file_id.has_value() && !request.target_file_id->empty();
  if (request.folder_id.empty() && !adding_version) {
    RespondError(callback, Error{"INVALID_ARGUMENT", "缺少 folder_id"});
    return;
  }
  if (request.file_name.empty() && !adding_version) {
    RespondError(callback, Error{"INVALID_ARGUMENT", "缺少 file_name"});
    return;
  }
  if (!body.contains("size_bytes") && !body.contains("total_size")) {
    RespondError(callback, Error{"INVALID_ARGUMENT", "缺少 size_bytes"});
    return;
  }

  // 限流（阶段十二）：按用户限制创建上传会话的频率。
  // 与阶段五的"并发会话上限"互补：那个限制同时在传的数量，这个限制单位时间的次数。
  {
    const Actor actor = ActorFrom(req);
    const auto decision = context->upload_limiter->Acquire("upload|" + actor.user_id);
    if (!decision.allowed) {
      auto response = MakeErrorResponse(Error{"RATE_LIMITED", "创建上传会话过于频繁，请稍后再试"},
                                        CurrentRequestId());
      response->addHeader("Retry-After", std::to_string(decision.retry_after_seconds));
      spdlog::warn("上传限流触发 | user={}", actor.user_id);
      callback(response);
      return;
    }
  }

  auto created = context->uploads_service->CreateSession(ActorFrom(req), request);
  if (!created) {
    RespondError(callback, created.GetError());
    return;
  }
  callback(MakeSuccessResponse(CreateResponse(created.Value()), CurrentRequestId()));
}

void UploadController::Status(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              std::string upload_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }
  auto status = context->uploads_service->GetStatus(ActorFrom(req), upload_id);
  if (!status) {
    RespondError(callback, status.GetError());
    return;
  }
  callback(MakeSuccessResponse(StatusResponse(status.Value()), CurrentRequestId()));
}

void UploadController::Chunk(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             std::string upload_id, std::string chunk_index) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto index = ParseChunkIndex(chunk_index);
  if (!index) {
    RespondError(callback, index.GetError());
    return;
  }

  // 分块级哈希：客户端可选声明，服务端始终自己重算
  const std::string client_chunk_hash = req->getHeader("X-Chunk-Sha256");

  auto start = context->uploads_service->BeginChunk(ActorFrom(req), upload_id, index.Value(),
                                                    client_chunk_hash);
  if (!start) {
    RespondError(callback, start.GetError());
    return;
  }

  // 已存在且内容一致：幂等成功，不重复写盘
  if (start.Value().already_present) {
    // 同一分块重复上传 = 客户端重试，单独计数便于判断网络质量
    Metrics().IncrementCounter("lan_file_hub_upload_chunk_retries_total",
                               "重复上传同一分块的次数（客户端重试）");
    callback(MakeSuccessResponse(
        nlohmann::json{{"chunk_index", index.Value()}, {"already_present", true}},
        CurrentRequestId()));
    return;
  }

  const std::shared_ptr<ChunkUploadSession> session = start.Value().session;

  // 与单文件上传同样的双路径：小请求体已被 Drogon 缓冲，大请求体走流式。
  // main.cpp 把内存上限设成 1 MiB，因此大分块必然是流式的。
  auto stream = drogon::internal::createRequestStream(req);
  if (!stream) {
    const auto body = req->getBody();
    auto written = session->Write(body.data(), body.size());
    if (!written.Ok()) {
      session->Abort();
      RespondError(callback, written.GetError());
      return;
    }
    auto finished = session->Finish();
    if (!finished.Ok()) {
      RespondError(callback, finished.GetError());
      return;
    }
    callback(MakeSuccessResponse(
        nlohmann::json{{"chunk_index", index.Value()}, {"size_bytes", session->bytes_written()}},
        CurrentRequestId()));
    return;
  }

  stream->setStreamReader(drogon::RequestStreamReader::newReader(
      [session](const char* data, std::size_t length) {
        const auto status = session->Write(data, length);
        if (!status.Ok()) {
          throw drogon::StreamError(drogon::StreamErrorCode::kBadRequest,
                                    status.GetError().message);
        }
      },
      [session, index, callback](std::exception_ptr error) {
        if (error) {
          std::string reason = "分块上传中断";
          try {
            std::rethrow_exception(error);
          } catch (const drogon::StreamError& stream_error) {
            reason = stream_error.what();
          } catch (const std::exception& ex) {
            reason = ex.what();
          }
          session->Abort();
          RespondError(callback, Error{"CHUNK_INVALID", reason});
          return;
        }

        auto finished = session->Finish();
        if (!finished.Ok()) {
          RespondError(callback, finished.GetError());
          return;
        }
        callback(MakeSuccessResponse(nlohmann::json{{"chunk_index", index.Value()},
                                                    {"size_bytes", session->bytes_written()}},
                                     CurrentRequestId()));
      }));
}

void UploadController::Complete(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                std::string upload_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto file = context->uploads_service->Complete(ActorFrom(req), upload_id);
  if (!file) {
    Metrics().IncrementCounter("lan_file_hub_uploads_total", "上传完成结果计数",
                               {{"result", "failed"}});
    RespondError(callback, file.GetError());
    return;
  }

  const File& created = file.Value();
  Metrics().IncrementCounter("lan_file_hub_uploads_total", "上传完成结果计数",
                             {{"result", "success"}});
  Metrics().IncrementCounter("lan_file_hub_upload_bytes_total", "上传成功写入的字节数", {},
                             created.size_bytes);
  callback(MakeSuccessResponse(
      nlohmann::json{
          {"id", created.id},
          {"folder_id", created.folder_id},
          {"name", created.original_name},
          {"mime_type", created.mime_type},
          {"size_bytes", created.size_bytes},
          {"content_hash", created.content_hash},
          {"current_version_id", created.current_version_id.has_value()
                                     ? nlohmann::json(*created.current_version_id)
                                     : nlohmann::json(nullptr)},
          {"status", ToDbString(created.status)},
          {"created_at", created.created_at},
          {"updated_at", created.updated_at},
      },
      CurrentRequestId()));
}

void UploadController::Cancel(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              std::string upload_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }
  auto status = context->uploads_service->Cancel(ActorFrom(req), upload_id);
  if (!status.Ok()) {
    RespondError(callback, status.GetError());
    return;
  }
  callback(MakeSuccessResponse(nlohmann::json{{"cancelled", true}, {"upload_id", upload_id}},
                               CurrentRequestId()));
}

}  // namespace lan_file_hub
