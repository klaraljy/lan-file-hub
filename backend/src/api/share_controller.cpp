#include "api/share_controller.h"

#include <string>
#include <utility>

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "api/http_metrics.h"
#include "lan_file_hub/api/download_streamer.h"
#include "lan_file_hub/api/error_response.h"
#include "lan_file_hub/api/json_body.h"
#include "lan_file_hub/application/app_context.h"
#include "lan_file_hub/common/http_util.h"
#include "lan_file_hub/common/metrics.h"
#include "lan_file_hub/common/rate_limiter.h"
#include "lan_file_hub/common/request_id.h"
#include "lan_file_hub/common/sha256.h"
#include "lan_file_hub/infrastructure/permission_repository.h"

namespace lan_file_hub {
namespace {

// 公开分享的响应体（Describe 与 Unlock 共用，避免两处漂移）。
// ⛔ 绝不含 owner_id / storage_key / token_hash 这类内部标识。
nlohmann::json PublicShareToJson(const PublicShareView& share) {
  return nlohmann::json{
      {"name", share.name},
      {"resource_type", share.resource_type},
      {"size_bytes", share.size_bytes},
      {"mime_type", share.mime_type},
      {"permission", share.permission},
      {"requires_password", share.requires_password},
      {"expires_at",
       share.expires_at.has_value() ? nlohmann::json(*share.expires_at) : nlohmann::json(nullptr)},
      {"download_count", share.download_count},
      {"max_downloads", share.max_downloads.has_value() ? nlohmann::json(*share.max_downloads)
                                                        : nlohmann::json(nullptr)},
      {"can_download", share.can_download},
      {"download_blocked_reason", share.download_blocked_reason},
  };
}

// 列表里**不回传 token_hash**：它虽然是哈希，但没有必要离开服务端
nlohmann::json ShareToJson(const ShareLink& link) {
  nlohmann::json json{
      {"id", link.id},
      {"resource_type", link.resource_type},
      {"resource_id", link.resource_id},
      {"resource_name", link.resource_name},
      {"permission", link.permission},
      {"download_count", link.download_count},
      // 只有"这条有没有设密码"这一个布尔值离开服务端；哈希本身永不出门（2026-10-03）
      {"requires_password", !link.password_hash.empty()},
      {"created_by", link.created_by},
      {"created_at", link.created_at},
      {"revoked", link.revoked_at.has_value()},
      {"revoked_at",
       link.revoked_at.has_value() ? nlohmann::json(*link.revoked_at) : nlohmann::json(nullptr)},
      {"expires_at",
       link.expires_at.has_value() ? nlohmann::json(*link.expires_at) : nlohmann::json(nullptr)},
      {"max_downloads", link.max_downloads.has_value() ? nlohmann::json(*link.max_downloads)
                                                       : nlohmann::json(nullptr)},
      {"last_access_at", link.last_access_at.has_value() ? nlohmann::json(*link.last_access_at)
                                                         : nlohmann::json(nullptr)},
  };
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
    spdlog::warn("分享请求失败 | code={} | detail={}", error.code, RedactWithContext(error.detail));
  }
  callback(MakeErrorResponse(error, CurrentRequestId()));
}

}  // namespace

void ShareController::Create(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             std::string resource_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  ShareCreateRequest request;
  // 分享创建允许空请求体（用默认权限），但一旦带体就必须是合法且在上限内的 JSON
  if (!req->getBody().empty()) {
    auto parsed_body = ParseJsonBody(req);
    if (!parsed_body) {
      RespondError(callback, parsed_body.GetError());
      return;
    }
    const nlohmann::json& body = parsed_body.Value();
    if (body.contains("permission") && body["permission"].is_string()) {
      request.permission = body["permission"].get<std::string>();
    }
    if (body.contains("expires_in_seconds") && body["expires_in_seconds"].is_number_integer()) {
      request.expires_in_seconds = body["expires_in_seconds"].get<std::int64_t>();
    }
    if (body.contains("max_downloads") && body["max_downloads"].is_number_integer()) {
      request.max_downloads = body["max_downloads"].get<std::int32_t>();
    }
    // 分享密码（2026-10-03）：只在这里读一次明文，服务层立刻哈希，之后再也拿不到
    if (body.contains("password") && body["password"].is_string()) {
      const std::string password = body["password"].get<std::string>();
      if (!password.empty()) {
        request.password = password;
      }
    }
  }

  auto created = context->sharing_service->CreateShare(ActorFrom(req), resource_id, request);
  if (!created) {
    RespondError(callback, created.GetError());
    return;
  }

  nlohmann::json payload = ShareToJson(created.Value().link);
  // 明文 Token 只在这里出现一次，之后服务端也无法再取回
  payload["token"] = created.Value().plaintext_token;
  payload["url"] = "/api/v1/public/shares/" + created.Value().plaintext_token;
  payload["download_allowed"] = created.Value().download_allowed;
  payload["token_visible_once"] = true;
  callback(MakeSuccessResponse(payload, CurrentRequestId()));
}

void ShareController::List(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           std::string resource_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto links = context->sharing_service->ListShares(ActorFrom(req), resource_id);
  if (!links) {
    RespondError(callback, links.GetError());
    return;
  }

  nlohmann::json items = nlohmann::json::array();
  for (const auto& link : links.Value()) {
    items.push_back(ShareToJson(link));
  }
  callback(MakeSuccessResponse(
      nlohmann::json{{"items", std::move(items)}, {"total", links.Value().size()}},
      CurrentRequestId()));
}

void ShareController::Revoke(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             std::string share_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto status = context->sharing_service->RevokeShare(ActorFrom(req), share_id);
  if (!status.Ok()) {
    RespondError(callback, status.GetError());
    return;
  }
  callback(
      MakeSuccessResponse(nlohmann::json{{"id", share_id}, {"revoked", true}}, CurrentRequestId()));
}

void ShareController::PublicDescribe(const drogon::HttpRequestPtr& /*req*/,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                     std::string token) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto view = context->sharing_service->DescribePublic(token);
  if (!view) {
    RespondError(callback, view.GetError());
    return;
  }

  callback(MakeSuccessResponse(PublicShareToJson(view.Value()), CurrentRequestId()));
}

void ShareController::PublicUnlock(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                   std::string token) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  // 速率限制（防止拿解锁接口暴力猜密码）：按 IP + Token 计数，两个维度都要有。
  // 与登录限流同一个思路：谁能拿到 token 谁就能猜，所以要按 token 单独限。
  const std::string client_ip = ClientIpFrom(
      req->getHeader("X-Forwarded-For"), req->getHeader("X-Real-IP"), req->getPeerAddr().toIp());
  if (context->share_unlock_limiter) {
    const auto decision = context->share_unlock_limiter->Acquire(client_ip + "|" + token);
    if (!decision.allowed) {
      RespondError(callback, Error{"RATE_LIMITED", "尝试次数太多，请稍后再试"});
      return;
    }
  }

  std::string password;
  if (!req->getBody().empty()) {
    auto body = ParseJsonBody(req);
    if (!body) {
      RespondError(callback, body.GetError());
      return;
    }
    if (body.Value().contains("password") && body.Value()["password"].is_string()) {
      password = body.Value()["password"].get<std::string>();
    }
  }

  auto unlocked = context->sharing_service->UnlockPublic(token, password);
  if (!unlocked) {
    RespondError(callback, unlocked.GetError());
    return;
  }

  nlohmann::json payload = PublicShareToJson(unlocked.Value().share);
  if (!unlocked.Value().download_ticket.empty()) {
    // 票据拼进下载地址：浏览器原生下载时带不了 Authorization 头（AGENTS §15.2）
    payload["ticket"] = unlocked.Value().download_ticket;
    payload["ticket_expires_in"] = unlocked.Value().ticket_expires_in_seconds;
    payload["download_url"] =
        "/api/v1/public/shares/" + token + "/download?ticket=" + unlocked.Value().download_ticket;
  } else {
    payload["download_url"] = "/api/v1/public/shares/" + token + "/download";
  }
  callback(MakeSuccessResponse(payload, CurrentRequestId()));
}

void ShareController::PublicDownload(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                     std::string token) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  // 需要密码的分享必须带解锁时拿到的票据（⛔ 否则直接访问这个 URL 就绕过了密码）
  const std::string ticket = req->getParameter("ticket");
  auto download = context->sharing_service->PreparePublicDownload(token, ticket);
  if (!download) {
    Metrics().IncrementCounter(
        "lan_file_hub_downloads_total", "下载结果计数",
        {{"result", "failed"}, {"reason", DownloadFailureReason(download.GetError().code)}});
    RespondError(callback, download.GetError());
    return;
  }

  const File& file = download.Value().file;
  const std::string& path = download.Value().absolute_path;
  const std::string range_header = req->getHeader("Range");

  auto range = ParseByteRange(range_header, file.size_bytes);
  if (!range) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k416RequestedRangeNotSatisfiable);
    response->addHeader("Content-Range", "bytes */" + std::to_string(file.size_bytes));
    callback(response);
    return;
  }

  // 限速下载（阶段十二第 4 条）：公开下载没有登录用户，按来源 IP 限速。
  // 刻意不把分享 Token 放进限速 key —— key 会进日志，而 Token 不允许进日志。
  const auto limiter = context->download_limiter;
  const std::string rate_key =
      "public|" + ClientIpFrom(req->getHeader("X-Forwarded-For"), req->getHeader("X-Real-IP"),
                               req->getPeerAddr().toIp());
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
  response->addHeader("Content-Disposition", BuildContentDisposition(file.original_name));
  // 公开内容不允许被搜索引擎索引，也不要把分享页面缓存到中间代理
  response->addHeader("X-Robots-Tag", "noindex, nofollow");
  response->addHeader("Cache-Control", "private, no-store");

  // 不记录 Token 的任何片段（哪怕是前 6 位）：需要跨日志关联时用不可逆的短哈希
  Metrics().IncrementCounter("lan_file_hub_downloads_total", "下载结果计数",
                             {{"result", "success"}, {"reason", "none"}});
  spdlog::info("公开分享下载 | share_ref={} | file_id={} | bytes={}", Sha256Hex(token).substr(0, 8),
               file.id, range.Value().length);
  callback(response);
}

}  // namespace lan_file_hub
