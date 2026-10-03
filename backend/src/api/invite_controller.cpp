#include "api/invite_controller.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "lan_file_hub/api/error_response.h"
#include "lan_file_hub/api/json_body.h"
#include "lan_file_hub/application/app_context.h"
#include "lan_file_hub/application/invite_service.h"
#include "lan_file_hub/common/request_id.h"
#include "lan_file_hub/infrastructure/invite_repository.h"

namespace lan_file_hub {
namespace {

void RespondError(const std::function<void(const drogon::HttpResponsePtr&)>& callback,
                  const Error& error) {
  callback(MakeErrorResponse(error, CurrentRequestId()));
}

// 只有管理员能建/看/撤销邀请。AuthFilter 已确认登录，这里再判角色
//（权限一律在后端校验，前端不显示按钮不算控制，AGENTS.md §8）。
bool IsAdmin(const drogon::HttpRequestPtr& req) {
  try {
    return req->attributes()->get<std::string>("user_role") == "ADMIN";
  } catch (const std::exception&) {
    return false;
  }
}

nlohmann::json NullableString(const std::optional<std::string>& value) {
  return value.has_value() ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

// 管理员看到的邀请：**不含 token**（库里只有哈希，谁也拿不回明文）
nlohmann::json InviteToJson(const Invite& invite) {
  return nlohmann::json{
      {"id", invite.id},
      {"note", invite.note},
      {"created_by", invite.created_by},
      {"created_at", invite.created_at},
      {"expires_at", NullableString(invite.expires_at)},
      {"revoked_at", NullableString(invite.revoked_at)},
      {"max_uses", invite.max_uses},
      {"used_count", invite.used_count},
      {"remaining_uses", invite.RemainingUses()},
  };
}

// 公开校验只回"这条邀请还能不能用"，不回是谁建的、id 是多少（最小暴露）
nlohmann::json PublicInviteToJson(const Invite& invite) {
  return nlohmann::json{
      {"note", invite.note},
      {"expires_at", NullableString(invite.expires_at)},
      {"remaining_uses", invite.RemainingUses()},
  };
}

std::string ReadStringField(const nlohmann::json& body, const char* key) {
  if (!body.contains(key) || !body[key].is_string()) {
    return {};
  }
  return body[key].get<std::string>();
}

}  // namespace

void InviteController::Create(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context || !context->invites) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }
  if (!IsAdmin(req)) {
    RespondError(callback, Error{"FORBIDDEN", "只有管理员能发邀请"});
    return;
  }

  auto body = ParseJsonBody(req);
  if (!body) {
    RespondError(callback, body.GetError());
    return;
  }

  InviteService::CreateRequest request;
  request.note = ReadStringField(body.Value(), "note");
  if (body.Value().contains("max_uses") && body.Value()["max_uses"].is_number_integer()) {
    request.max_uses = body.Value()["max_uses"].get<std::int32_t>();
  }
  if (body.Value().contains("expires_in_seconds") &&
      body.Value()["expires_in_seconds"].is_number_integer()) {
    request.expires_in_seconds = body.Value()["expires_in_seconds"].get<std::int64_t>();
  }

  const std::string admin_id = req->attributes()->get<std::string>("user_id");
  auto created = context->invites->Create(request, admin_id);
  if (!created) {
    RespondError(callback, created.GetError());
    return;
  }

  // 日志只记 id 与哈希前缀，**绝不记 token 明文**（与分享链接同口径）
  spdlog::info("创建邀请 | invite_id={} | by={} | max_uses={} | token_ref={}", created.Value().invite.id,
               admin_id, created.Value().invite.max_uses,
               HashInviteToken(created.Value().token).substr(0, 8));

  callback(MakeSuccessResponse(
      nlohmann::json{{"invite", InviteToJson(created.Value().invite)},
                     {"token", created.Value().token}},  // 明文只在这里出现一次
      CurrentRequestId()));
}

void InviteController::List(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context || !context->invites) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }
  if (!IsAdmin(req)) {
    RespondError(callback, Error{"FORBIDDEN", "只有管理员能查看邀请"});
    return;
  }

  int page = 1;
  int page_size = 20;
  try {
    if (!req->getParameter("page").empty()) {
      page = std::stoi(req->getParameter("page"));
    }
    if (!req->getParameter("page_size").empty()) {
      page_size = std::stoi(req->getParameter("page_size"));
    }
  } catch (const std::exception&) {
    RespondError(callback, Error{"INVALID_ARGUMENT", "分页参数必须是整数"});
    return;
  }
  if (page < 1) {
    page = 1;
  }

  auto invites = context->invites->List(page_size, (page - 1) * page_size);
  if (!invites) {
    RespondError(callback, invites.GetError());
    return;
  }
  auto total = context->invites->Count();
  if (!total) {
    RespondError(callback, total.GetError());
    return;
  }

  nlohmann::json items = nlohmann::json::array();
  for (const auto& invite : invites.Value()) {
    items.push_back(InviteToJson(invite));
  }
  callback(MakeSuccessResponse(nlohmann::json{{"items", std::move(items)},
                                              {"page", page},
                                              {"page_size", page_size},
                                              {"total", total.Value()}},
                               CurrentRequestId()));
}

void InviteController::Revoke(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              std::string invite_id) {
  const auto context = GetAppContext();
  if (!context || !context->invites) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }
  if (!IsAdmin(req)) {
    RespondError(callback, Error{"FORBIDDEN", "只有管理员能撤销邀请"});
    return;
  }

  const Status status = context->invites->Revoke(invite_id);
  if (!status.Ok()) {
    RespondError(callback, status.GetError());
    return;
  }

  spdlog::info("撤销邀请 | invite_id={} | by={}", invite_id,
               req->attributes()->get<std::string>("user_id"));
  callback(MakeSuccessResponse(nlohmann::json{{"revoked", true}}, CurrentRequestId()));
}

void InviteController::Describe(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                std::string token) {
  (void)req;
  const auto context = GetAppContext();
  if (!context || !context->invites) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto invite = context->invites->Describe(token);
  if (!invite) {
    RespondError(callback, invite.GetError());
    return;
  }
  callback(MakeSuccessResponse(PublicInviteToJson(invite.Value()), CurrentRequestId()));
}

void InviteController::RegistrationPolicy(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  (void)req;
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }
  // 注册页据此决定"要不要显示邀请输入/提示找管理员要链接"
  callback(MakeSuccessResponse(
      nlohmann::json{{"self_registration", context->allow_self_registration}},
      CurrentRequestId()));
}

}  // namespace lan_file_hub
