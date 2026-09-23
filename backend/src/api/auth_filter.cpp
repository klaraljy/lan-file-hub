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

  const auto context = GetAppContext();
  if (!context) {
    Reject(fcb, Error{"INTERNAL_ERROR", "认证服务未初始化"});
    return;
  }

  const std::string token = ExtractBearerToken(req->getHeader("Authorization"));
  if (token.empty()) {
    // 没有会话令牌时，允许用「下载票据」通过。
    // 为什么必须支持：大文件下载要让**浏览器自己**流式落盘（不再把整个文件读进内存），
    // 而浏览器直接导航下载时带不了 Authorization 头，所以票据放进 URL 查询参数。
    const std::string ticket = req->getParameter("ticket");
    if (ticket.empty()) {
      // 同时覆盖「没有 Authorization 头」和「不是 Bearer 形式」两种情况
      Reject(fcb, Error{"UNAUTHENTICATED", "缺少访问令牌"});
      return;
    }

    auto ticket_claims = context->jwt->VerifyDownloadTicket(ticket);
    if (!ticket_claims) {
      Reject(fcb, ticket_claims.GetError());
      return;
    }
    // 票据只能用于它自己绑定的那个文件（路径参数可用时先挡一层，handler 里还有权威校验）
    const std::string file_param = req->getParameter("file_id");
    if (!file_param.empty() && file_param != ticket_claims.Value().file_id) {
      Reject(fcb, Error{"FORBIDDEN", "下载票据与文件不匹配"});
      return;
    }

    auto ticket_user = context->auth->CurrentUser(ticket_claims.Value().user_id);
    if (!ticket_user) {
      Reject(fcb, ticket_user.GetError());
      return;
    }

    req->attributes()->insert("user_id", ticket_user.Value().id);
    req->attributes()->insert("user_role", ToDbString(ticket_user.Value().role));
    req->attributes()->insert("username", ticket_user.Value().username);
    req->attributes()->insert("download_ticket_file_id", ticket_claims.Value().file_id);
    req->attributes()->insert("download_ticket_version_id", ticket_claims.Value().version_id);
    fccb();
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
