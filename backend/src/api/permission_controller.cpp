#include "api/permission_controller.h"

#include <string>
#include <utility>

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "lan_file_hub/api/error_response.h"
#include "lan_file_hub/api/json_body.h"
#include "lan_file_hub/application/app_context.h"
#include "lan_file_hub/common/request_id.h"
#include "lan_file_hub/infrastructure/permission_repository.h"

namespace lan_file_hub {
namespace {

nlohmann::json GrantToJson(const PermissionGrant& grant) {
  return nlohmann::json{
      {"id", grant.id},
      {"resource_type", grant.resource_type},
      {"resource_id", grant.resource_id},
      {"subject_type", grant.subject_type},
      {"subject_id", grant.subject_id},
      {"subject_name", grant.subject_name},
      {"permission", grant.permission},
      {"created_at", grant.created_at},
      {"updated_at", grant.updated_at},
  };
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
    spdlog::warn("权限请求失败 | code={} | detail={}", error.code, RedactWithContext(error.detail));
  }
  callback(MakeErrorResponse(error, CurrentRequestId()));
}

}  // namespace

void PermissionController::List(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                std::string resource_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto grants = context->sharing_service->ListGrants(ActorFrom(req), resource_id);
  if (!grants) {
    RespondError(callback, grants.GetError());
    return;
  }

  nlohmann::json items = nlohmann::json::array();
  for (const auto& grant : grants.Value()) {
    items.push_back(GrantToJson(grant));
  }
  callback(MakeSuccessResponse(
      nlohmann::json{{"items", std::move(items)}, {"total", grants.Value().size()}},
      CurrentRequestId()));
}

void PermissionController::Grant(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                 std::string resource_id, std::string user_id) {
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
  if (!body.contains("permission") || !body["permission"].is_string()) {
    RespondError(callback, Error{"INVALID_ARGUMENT", "缺少 permission"});
    return;
  }

  auto grant = context->sharing_service->Grant(ActorFrom(req), resource_id, user_id,
                                               body["permission"].get<std::string>());
  if (!grant) {
    RespondError(callback, grant.GetError());
    return;
  }
  callback(MakeSuccessResponse(GrantToJson(grant.Value()), CurrentRequestId()));
}

void PermissionController::Revoke(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                  std::string resource_id, std::string user_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto status = context->sharing_service->RevokeGrant(ActorFrom(req), resource_id, user_id);
  if (!status.Ok()) {
    RespondError(callback, status.GetError());
    return;
  }
  callback(MakeSuccessResponse(
      nlohmann::json{{"resource_id", resource_id}, {"user_id", user_id}, {"revoked", true}},
      CurrentRequestId()));
}

void PermissionController::Effective(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                     std::string resource_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto level = context->sharing_service->EffectiveLevel(ActorFrom(req), resource_id);
  if (!level) {
    RespondError(callback, level.GetError());
    return;
  }
  callback(MakeSuccessResponse(
      nlohmann::json{{"resource_id", resource_id}, {"level", level.Value()}}, CurrentRequestId()));
}

void PermissionController::SharedWithMe(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto items = context->sharing_service->SharedWith(ActorFrom(req), 200);
  if (!items) {
    RespondError(callback, items.GetError());
    return;
  }

  nlohmann::json payload = nlohmann::json::array();
  for (const auto& item : items.Value()) {
    payload.push_back(nlohmann::json{
        {"resource_type", item.resource_type},
        {"resource_id", item.resource_id},
        {"name", item.name},
        {"size_bytes", item.size_bytes},
        {"permission", item.permission},
        {"owner_id", item.owner_id},
        {"granted_at", item.granted_at},
    });
  }
  callback(MakeSuccessResponse(
      nlohmann::json{{"items", std::move(payload)}, {"total", items.Value().size()}},
      CurrentRequestId()));
}

}  // namespace lan_file_hub
