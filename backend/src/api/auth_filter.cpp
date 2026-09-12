#include "api/auth_filter.h"

#include <string>

#include <drogon/drogon.h>

#include "lan_file_hub/api/error_response.h"
#include "lan_file_hub/application/app_context.h"
#include "lan_file_hub/common/request_id.h"

namespace {

constexpr char kBearerPrefix[] = "Bearer ";

// 从 Authorization 头里取出 Token；格式不对返回空串
std::string ExtractBearerToken(const std::string& header) {
  if (header.size() <= sizeof(kBearerPrefix) - 1)
    return {};
  if (header.compare(0, sizeof(kBearerPrefix) - 1, kBearerPrefix) != 0)
    return {};
  std::string token = header.substr(sizeof(kBearerPrefix) - 1);
  while (!token.empty() && (token.back() == ' ' || token.back() == '\r' || token.back() == '\n')) {
    token.pop_back();
  }
  return token;
}

void Reject(const drogon::FilterCallback& reject, lan_file_hub::Error error) {
  reject(lan_file_hub::MakeErrorResponse(error, lan_file_hub::CurrentRequestId()));
}

}  // namespace

void AuthFilter::doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& fcb,
                          drogon::FilterChainCallback&& fccb) {
  using namespace lan_file_hub;

  const std::string token = ExtractBearerToken(req->getHeader("Authorization"));
  if (token.empty()) {
    // 同时覆盖「没有 Authorization 头」和「不是 Bearer 形式」两种情况
    Reject(fcb, Error{"UNAUTHENTICATED", "缺少访问令牌"});
    return;
  }

  const auto context = GetAppContext();
  if (!context) {
    Reject(fcb, Error{"INTERNAL_ERROR", "认证服务未初始化"});
    return;
  }

  auto claims = context->jwt->Verify(token);
  if (!claims) {
    Reject(fcb, claims.GetError());
    return;
  }

  // 复查用户状态：Token 未过期不代表账号仍然可用（docs/api.md §认证方案）
  auto user = context->auth->CurrentUser(claims.Value().user_id);
  if (!user) {
    Reject(fcb, user.GetError());
    return;
  }

  req->attributes()->insert("user_id", user.Value().id);
  req->attributes()->insert("user_role", ToDbString(user.Value().role));
  req->attributes()->insert("username", user.Value().username);
  fccb();
}
