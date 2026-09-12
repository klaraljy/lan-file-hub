#include "lan_file_hub/api/security_headers.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>

#include <drogon/drogon.h>

namespace lan_file_hub {
namespace {

// 允许的请求头（前端分块上传要带 X-Chunk-Sha256，版本并发控制要带 If-Match）
constexpr const char* kAllowedRequestHeaders =
    "Authorization, Content-Type, X-Chunk-Sha256, If-Match, If-None-Match, Range, X-Request-Id";
// 允许前端读取的响应头（下载文件名、断点续传、限流退避都要用到）
constexpr const char* kExposedResponseHeaders =
    "Content-Disposition, Content-Range, Content-Length, Accept-Ranges, X-File-Version, "
    "Retry-After, X-Request-Id";

std::string NormalizeOrigin(std::string_view origin) {
  std::size_t begin = 0;
  std::size_t end = origin.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(origin[begin])) != 0) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(origin[end - 1])) != 0) {
    --end;
  }
  std::string normalized(origin.substr(begin, end - begin));
  while (!normalized.empty() && normalized.back() == '/') {
    normalized.pop_back();
  }
  std::ranges::transform(normalized, normalized.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return normalized;
}

void ApplyCorsHeaders(const drogon::HttpResponsePtr& response, const std::string& origin) {
  response->addHeader("Access-Control-Allow-Origin", origin);
  // 响应随 Origin 变化，必须声明，否则中间缓存会把 A 站的响应发给 B 站
  response->addHeader("Vary", "Origin");
  response->addHeader("Access-Control-Expose-Headers", kExposedResponseHeaders);
}

void ApplySecurityHeaders(const drogon::HttpRequestPtr& req,
                          const drogon::HttpResponsePtr& response) {
  // 浏览器不得根据内容猜类型（上传的文件可能被当成 HTML 执行）
  response->addHeader("X-Content-Type-Options", "nosniff");
  // 本服务的响应不允许被任何页面用 iframe 嵌入
  response->addHeader("X-Frame-Options", "DENY");
  response->addHeader("Referrer-Policy", "no-referrer");
  response->addHeader("X-XSS-Protection", "0");  // 关闭已废弃的浏览器 XSS 审计器
  response->addHeader("Permissions-Policy", "geolocation=(), microphone=(), camera=()");
  // API 只输出 JSON 与文件，不应被当成文档执行任何脚本或表单
  response->addHeader(
      "Content-Security-Policy",
      "default-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'");
  if (req->isOnSecureConnection()) {
    response->addHeader("Strict-Transport-Security", "max-age=31536000; includeSubDomains");
  }
}

}  // namespace

bool IsOriginAllowed(const std::vector<std::string>& allowed_origins, std::string_view origin) {
  if (origin.empty() || origin == "*") {
    return false;
  }
  const std::string needle = NormalizeOrigin(origin);
  if (needle.empty() || needle == "*") {
    return false;
  }
  return std::ranges::any_of(allowed_origins, [&needle](const std::string& candidate) {
    if (candidate == "*") {
      return false;  // 配置层已禁止通配，这里再兜一层
    }
    return NormalizeOrigin(candidate) == needle;
  });
}

void RegisterSecurityAdvices(const std::vector<std::string>& allowed_origins) {
  auto allowed = std::make_shared<std::vector<std::string>>(allowed_origins);

  // 1) 预检：必须在路由之前应答。命中白名单回 204 + 允许的方法/头；
  //    不在白名单就明确 403，而不是让浏览器去猜。
  drogon::app().registerPreRoutingAdvice([allowed](const drogon::HttpRequestPtr& req,
                                                   drogon::AdviceCallback&& callback,
                                                   drogon::AdviceChainCallback&& chain) {
    if (req->method() != drogon::Options) {
      chain();
      return;
    }
    const std::string origin = req->getHeader("Origin");
    if (!IsOriginAllowed(*allowed, origin)) {
      auto forbidden = drogon::HttpResponse::newHttpResponse();
      forbidden->setStatusCode(drogon::k403Forbidden);
      forbidden->addHeader("Vary", "Origin");
      callback(forbidden);
      return;
    }
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k204NoContent);
    ApplyCorsHeaders(response, origin);
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", kAllowedRequestHeaders);
    response->addHeader("Access-Control-Max-Age", "600");
    callback(response);
  });

  // 2) 发送前：所有响应补安全头；带 Origin 且命中白名单时才补 CORS 头
  drogon::app().registerPreSendingAdvice(
      [allowed](const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& response) {
        ApplySecurityHeaders(req, response);
        const std::string origin = req->getHeader("Origin");
        if (origin.empty()) {
          return;
        }
        response->addHeader("Vary", "Origin");
        if (IsOriginAllowed(*allowed, origin)) {
          ApplyCorsHeaders(response, origin);
        }
        // 不在白名单：不回 CORS 头，由浏览器拦截（不要用 403 泄露白名单内容）
      });
}

}  // namespace lan_file_hub
