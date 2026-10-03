#include "api/auth_controller.h"

#include <string>
#include <utility>

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "lan_file_hub/api/error_response.h"
#include "lan_file_hub/api/json_body.h"
#include "lan_file_hub/application/app_context.h"
#include "lan_file_hub/common/rate_limiter.h"
#include "lan_file_hub/common/request_id.h"
#include "lan_file_hub/infrastructure/invite_repository.h"

namespace lan_file_hub {
namespace {

// 用户对外表示。**绝不包含 password_hash**（docs/security.md）。
nlohmann::json UserToJson(const User& user) {
  return nlohmann::json{
      {"id", user.id},
      {"username", user.username},
      {"email", user.email},
      {"role", ToDbString(user.role)},
      {"status", ToDbString(user.status)},
      {"storage_quota_bytes", user.storage_quota_bytes},
      {"used_storage_bytes", user.used_storage_bytes},
      {"created_at", user.created_at},
      {"updated_at", user.updated_at},
  };
}

nlohmann::json AuthResultToJson(const AuthResult& result) {
  return nlohmann::json{
      {"access_token", result.access_token},
      {"token_type", "Bearer"},
      {"expires_in", result.expires_in_seconds},
      {"user", UserToJson(result.user)},
  };
}

// 解析 JSON 请求体；失败时返回错误（不抛异常）
std::string ReadStringField(const nlohmann::json& body, const char* key) {
  if (!body.contains(key) || !body[key].is_string())
    return {};
  return body[key].get<std::string>();
}

// 「记住我」（用户 2026-10-03）：字段缺失或类型不对都当 false。
// ⛔ 默认必须是**短令牌**——宁可让用户多登一次，也不要因为解析写错而默默发出长效令牌。
bool ReadBoolField(const nlohmann::json& body, const char* key) {
  if (!body.contains(key) || !body[key].is_boolean())
    return false;
  return body[key].get<bool>();
}

void RespondError(const std::function<void(const drogon::HttpResponsePtr&)>& callback,
                  const Error& error) {
  // 失败原因进日志（已脱敏），但**不回显**给调用方
  if (!error.detail.empty()) {
    spdlog::warn("认证请求失败 | code={} | detail={}", error.code, RedactWithContext(error.detail));
  }
  callback(MakeErrorResponse(error, CurrentRequestId()));
}

}  // namespace

void AuthController::Register(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto body = ParseJsonBody(req);
  if (!body) {
    RespondError(callback, body.GetError());
    return;
  }

  RegisterRequest request;
  request.username = ReadStringField(body.Value(), "username");
  request.email = ReadStringField(body.Value(), "email");
  request.password = ReadStringField(body.Value(), "password");
  request.remember = ReadBoolField(body.Value(), "remember");

  // 邀请制注册（用户 2026-10-02）：关掉自助注册后**必须**带有效邀请；
  // 开着自助注册时也接受邀请（便于统计"这个人是被谁邀请来的"），邀请无效就当作没用邀请。
  const std::string invite_token = ReadStringField(body.Value(), "invite_token");
  std::optional<Invite> consumed_invite;
  if (!invite_token.empty() || !context->allow_self_registration) {
    if (invite_token.empty()) {
      RespondError(callback,
                   Error{"INVITE_REQUIRED", "本实例已关闭自助注册，请让管理员发一条邀请链接给你"});
      return;
    }
    if (!context->invites) {
      RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
      return;
    }
    auto consumed = context->invites->Consume(invite_token);
    if (!consumed) {
      RespondError(callback, consumed.GetError());
      return;
    }
    consumed_invite = consumed.Value();
  }

  auto result = context->auth->Register(request);
  if (!result) {
    // 注册失败要把刚消耗的那次还回去，否则用户填错一次就白白少一次机会
    if (consumed_invite.has_value() && context->invites) {
      (void)context->invites->Release(invite_token);
    }
    RespondError(callback, result.GetError());
    return;
  }

  if (consumed_invite.has_value()) {
    // 只记 id 与哈希前缀，**不记 token 明文**（与分享链接同口径）
    spdlog::info("邀请注册成功 | invite_id={} | token_ref={} | username={}", consumed_invite->id,
                 HashInviteToken(invite_token).substr(0, 8), result.Value().user.username);
  }

  // 只记录用户名，不记录口令或 Token
  spdlog::info("用户注册成功 | username={} | user_id={}", result.Value().user.username,
               result.Value().user.id);
  callback(MakeSuccessResponse(AuthResultToJson(result.Value()), CurrentRequestId()));
}

void AuthController::Login(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto body = ParseJsonBody(req);
  if (!body) {
    RespondError(callback, body.GetError());
    return;
  }

  LoginRequest request;
  request.identifier = ReadStringField(body.Value(), "username");
  if (request.identifier.empty()) {
    request.identifier = ReadStringField(body.Value(), "email");
  }
  request.password = ReadStringField(body.Value(), "password");
  request.remember = ReadBoolField(body.Value(), "remember");

  // 限流（阶段十二）：两个维度都要有。
  // 只看账号挡不住"换着用户名撞库"；只看 IP 会把同一个 NAT 出口后面的所有同事一起锁死，
  // 所以第二层（按 IP）刻意给得宽松。
  const std::string client_ip = ClientIpFrom(
      req->getHeader("X-Forwarded-For"), req->getHeader("X-Real-IP"), req->getPeerAddr().toIp());
  const std::string account_key = client_ip + "|" + request.identifier;

  const auto account_decision = context->login_limiter->Acquire(account_key);
  if (!account_decision.allowed) {
    auto response = MakeErrorResponse(Error{"RATE_LIMITED", "登录尝试过于频繁，请稍后再试"},
                                      CurrentRequestId());
    response->addHeader("Retry-After", std::to_string(account_decision.retry_after_seconds));
    spdlog::warn("登录限流触发（账号维度） | ip={} | identifier={}", client_ip, request.identifier);
    callback(response);
    return;
  }
  const auto ip_decision = context->login_ip_limiter->Acquire(client_ip);
  if (!ip_decision.allowed) {
    auto response = MakeErrorResponse(Error{"RATE_LIMITED", "该来源的登录尝试过于频繁，请稍后再试"},
                                      CurrentRequestId());
    response->addHeader("Retry-After", std::to_string(ip_decision.retry_after_seconds));
    spdlog::warn("登录限流触发（IP 维度） | ip={}", client_ip);
    callback(response);
    return;
  }

  auto result = context->auth->Login(request);
  if (!result) {
    // 失败时保留计数（计数就是用来累计失败的）；成功时才清零
    RespondError(callback, result.GetError());
    return;
  }

  // 登录成功：清掉这个 (IP + 账号) 的失败计数，避免"同事在共用出口上试错把别人拖下水"
  context->login_limiter->Reset(account_key);

  spdlog::info("用户登录成功 | username={} | user_id={}", result.Value().user.username,
               result.Value().user.id);
  callback(MakeSuccessResponse(AuthResultToJson(result.Value()), CurrentRequestId()));
}

void AuthController::Logout(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  const std::string user_id = req->attributes()->get<std::string>("user_id");
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto status = context->auth->Logout(user_id);
  if (!status.Ok()) {
    RespondError(callback, status.GetError());
    return;
  }

  spdlog::info("用户登出 | user_id={}", user_id);
  // 无状态 JWT：服务端没有会话可销毁，客户端丢弃 Token 即可
  callback(MakeSuccessResponse(nlohmann::json{{"logged_out", true}}, CurrentRequestId()));
}

void AuthController::Me(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  const std::string user_id = req->attributes()->get<std::string>("user_id");
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto user = context->auth->CurrentUser(user_id);
  if (!user) {
    RespondError(callback, user.GetError());
    return;
  }
  callback(MakeSuccessResponse(UserToJson(user.Value()), CurrentRequestId()));
}

}  // namespace lan_file_hub
