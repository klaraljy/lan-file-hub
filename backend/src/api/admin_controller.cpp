#include "lan_file_hub/api/admin_controller.h"

#include <string>
#include <utility>

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include "lan_file_hub/api/error_response.h"
#include "lan_file_hub/api/json_body.h"
#include "lan_file_hub/application/admin_service.h"
#include "lan_file_hub/application/app_context.h"
#include "lan_file_hub/application/folder_service.h"  // Actor
#include "lan_file_hub/common/pagination.h"
#include "lan_file_hub/common/request_id.h"

namespace lan_file_hub {
namespace {

Actor ActorFrom(const drogon::HttpRequestPtr& req) {
  Actor actor;
  actor.user_id = req->attributes()->get<std::string>("user_id");
  const std::string role = req->attributes()->get<std::string>("user_role");
  actor.role = (role == "ADMIN") ? UserRole::kAdmin : UserRole::kUser;
  return actor;
}

void RespondError(const std::function<void(const drogon::HttpResponsePtr&)>& callback,
                  const Error& error) {
  callback(MakeErrorResponse(error, CurrentRequestId()));
}

// 用户信息：**绝不包含 password_hash**（阶段十三红线）
nlohmann::json AdminUserToJson(const User& user) {
  return nlohmann::json{
      {"id", user.id},
      {"username", user.username},
      {"email", user.email},
      {"role", user.role == UserRole::kAdmin ? "ADMIN" : "USER"},
      {"status", user.status == UserStatus::kActive ? "ACTIVE" : "DISABLED"},
      {"storage_quota_bytes", user.storage_quota_bytes},
      {"used_storage_bytes", user.used_storage_bytes},
      {"created_at", user.created_at},
      {"updated_at", user.updated_at},
  };
}

nlohmann::json AuditEntryToJson(const AuditEntry& entry) {
  // detail_json 已经是序列化好的对象字符串；即使是字符串也照原样透传，不做二次解释
  nlohmann::json detail = nlohmann::json::object();
  if (!entry.detail_json.empty()) {
    detail = nlohmann::json::parse(entry.detail_json, nullptr, false);
    if (detail.is_discarded()) {
      detail = nlohmann::json{{"raw", entry.detail_json}};
    }
  }
  return nlohmann::json{
      {"id", entry.id},
      {"actor_id", entry.actor_id},
      {"action", entry.action},
      {"resource_type", entry.resource_type},
      {"resource_id", entry.resource_id},
      {"detail", std::move(detail)},
      {"created_at", entry.created_at},
  };
}

std::string Param(const drogon::HttpRequestPtr& req, const char* name) {
  return req->getParameter(name);
}

}  // namespace

void AdminController::ListUsers(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context || !context->admin_service) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto pagination = ParsePagination(Param(req, "page"), Param(req, "page_size"),
                                    Param(req, "sort_by"), Param(req, "sort_order"));
  if (!pagination) {
    RespondError(callback, pagination.GetError());
    return;
  }

  auto page = context->admin_service->ListUsers(
      ActorFrom(req), Param(req, "search"), Param(req, "status"), pagination.Value().sort_by,
      pagination.Value().sort_order == "desc", pagination.Value());
  if (!page) {
    RespondError(callback, page.GetError());
    return;
  }

  nlohmann::json users = nlohmann::json::array();
  for (const auto& user : page.Value().users) {
    users.push_back(AdminUserToJson(user));
  }
  callback(MakeSuccessResponse(nlohmann::json{{"items", std::move(users)},
                                              {"total", page.Value().total},
                                              {"page", pagination.Value().page},
                                              {"page_size", pagination.Value().page_size}},
                               CurrentRequestId()));
}

void AdminController::SetUserStatus(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                    std::string user_id) {
  const auto context = GetAppContext();
  if (!context || !context->admin_service) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto body = ParseJsonBody(req);
  if (!body) {
    RespondError(callback, body.GetError());
    return;
  }
  const auto& json = body.Value();
  if (!json.contains("status") || !json["status"].is_string()) {
    RespondError(callback, Error{"INVALID_ARGUMENT", "缺少 status"});
    return;
  }
  const std::string status = json["status"].get<std::string>();
  if (status != "ACTIVE" && status != "DISABLED") {
    RespondError(callback, Error{"INVALID_ARGUMENT", "status 只能是 ACTIVE 或 DISABLED"});
    return;
  }

  auto updated = context->admin_service->SetUserStatus(
      ActorFrom(req), user_id, status == "ACTIVE" ? UserStatus::kActive : UserStatus::kDisabled);
  if (!updated.Ok()) {
    RespondError(callback, updated.GetError());
    return;
  }
  callback(
      MakeSuccessResponse(nlohmann::json{{"id", user_id}, {"status", status}}, CurrentRequestId()));
}

void AdminController::SetUserQuota(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                   std::string user_id) {
  const auto context = GetAppContext();
  if (!context || !context->admin_service) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto body = ParseJsonBody(req);
  if (!body) {
    RespondError(callback, body.GetError());
    return;
  }
  const auto& json = body.Value();
  if (!json.contains("storage_quota_bytes") || !json["storage_quota_bytes"].is_number_unsigned()) {
    RespondError(callback, Error{"INVALID_ARGUMENT", "缺少 storage_quota_bytes"});
    return;
  }
  const auto quota = json["storage_quota_bytes"].get<std::uint64_t>();
  if (quota == 0) {
    RespondError(callback, Error{"INVALID_ARGUMENT", "配额必须大于 0"});
    return;
  }

  auto updated = context->admin_service->SetUserQuota(ActorFrom(req), user_id, quota);
  if (!updated.Ok()) {
    RespondError(callback, updated.GetError());
    return;
  }
  callback(MakeSuccessResponse(nlohmann::json{{"id", user_id}, {"storage_quota_bytes", quota}},
                               CurrentRequestId()));
}

void AdminController::StorageStats(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context || !context->admin_service) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto stats = context->admin_service->StorageStats(ActorFrom(req));
  if (!stats) {
    RespondError(callback, stats.GetError());
    return;
  }
  const auto& value = stats.Value();
  callback(MakeSuccessResponse(nlohmann::json{{"user_count", value.user_count},
                                              {"file_count", value.file_count},
                                              {"version_count", value.version_count},
                                              {"used_bytes", value.used_bytes},
                                              {"free_bytes", value.free_bytes},
                                              {"active_uploads", value.active_uploads}},
                               CurrentRequestId()));
}

void AdminController::QueryAuditLogs(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context || !context->admin_service) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto pagination = ParsePagination(Param(req, "page"), Param(req, "page_size"),
                                    Param(req, "sort_by"), Param(req, "sort_order"));
  if (!pagination) {
    RespondError(callback, pagination.GetError());
    return;
  }

  AuditLogFilter filter;
  filter.actor_id = Param(req, "actor_id");
  filter.action = Param(req, "action");
  filter.resource_type = Param(req, "resource_type");
  filter.resource_id = Param(req, "resource_id");

  auto page = context->admin_service->QueryAuditLogs(ActorFrom(req), filter, pagination.Value());
  if (!page) {
    RespondError(callback, page.GetError());
    return;
  }

  nlohmann::json entries = nlohmann::json::array();
  for (const auto& entry : page.Value().entries) {
    entries.push_back(AuditEntryToJson(entry));
  }
  callback(MakeSuccessResponse(nlohmann::json{{"items", std::move(entries)},
                                              {"total", page.Value().total},
                                              {"page", pagination.Value().page},
                                              {"page_size", pagination.Value().page_size}},
                               CurrentRequestId()));
}

}  // namespace lan_file_hub
