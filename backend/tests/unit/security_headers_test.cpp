// CORS 白名单判定单元测试（阶段十二第 11 条）。纯内存实现。
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "lan_file_hub/api/security_headers.h"

namespace lan_file_hub {
namespace {

TEST(SecurityHeadersTest, AllowsExactWhitelistedOrigin) {
  const std::vector<std::string> allowed{"http://localhost:5173", "https://files.example.com"};
  EXPECT_TRUE(IsOriginAllowed(allowed, "http://localhost:5173"));
  EXPECT_TRUE(IsOriginAllowed(allowed, "https://files.example.com"));
}

TEST(SecurityHeadersTest, RejectsUnknownOrigin) {
  const std::vector<std::string> allowed{"http://localhost:5173"};
  EXPECT_FALSE(IsOriginAllowed(allowed, "http://evil.example.com"));
  EXPECT_FALSE(IsOriginAllowed(allowed, "http://localhost:5174"));
  // 前缀相同但域名不同，不能放行
  EXPECT_FALSE(IsOriginAllowed(allowed, "http://localhost:5173.evil.com"));
}

TEST(SecurityHeadersTest, NeverTreatsWildcardAsAllowed) {
  // 配置层已经禁止 "*"；即便有人绕过配置传进来，这里也必须拒绝
  EXPECT_FALSE(IsOriginAllowed({"*"}, "http://localhost:5173"));
  EXPECT_FALSE(IsOriginAllowed({"*"}, "*"));
  EXPECT_FALSE(IsOriginAllowed({"http://localhost:5173"}, "*"));
  EXPECT_FALSE(IsOriginAllowed({"http://localhost:5173"}, ""));
}

TEST(SecurityHeadersTest, NormalisesCaseAndTrailingSlash) {
  const std::vector<std::string> allowed{"http://localhost:5173/"};
  EXPECT_TRUE(IsOriginAllowed(allowed, "http://LOCALHOST:5173"));
  EXPECT_TRUE(IsOriginAllowed(allowed, "  http://localhost:5173  "));
}

TEST(SecurityHeadersTest, EmptyWhitelistAllowsNothing) {
  EXPECT_FALSE(IsOriginAllowed({}, "http://localhost:5173"));
}

TEST(SecurityHeadersTest, WebPagesGetUsableCsp) {
  // 后端自托管前端时，页面自己的脚本/样式必须放行，否则浏览器白屏
  // （2026-10-02 实测：HTTP 200 但 CSP 把 /assets/index-*.js 拦了，界面什么都没有）
  for (const std::string path :
       {"/", "/index.html", "/assets/index-CZkY6Ifk.js", "/favicon.ico", "/files", "/login",
        "/s/sometoken", "/plaza"}) {
    const std::string csp = BuildContentSecurityPolicy(path);
    EXPECT_NE(csp.find("script-src 'self'"), std::string::npos) << path;
    EXPECT_NE(csp.find("style-src 'self' 'unsafe-inline'"), std::string::npos) << path;
    EXPECT_EQ(csp.find("default-src 'none'"), std::string::npos) << path;
    EXPECT_EQ(csp.find("http://"), std::string::npos) << path;   // 不放行任何外部来源
    EXPECT_EQ(csp.find("https://"), std::string::npos) << path;  // 同上（不含 CDN）
  }
}

TEST(SecurityHeadersTest, ApiAndHealthPathsKeepStrictCsp) {
  const std::string strict =
      "default-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'";
  for (const std::string path :
       {"/api", "/api/v1/files", "/api/v1/auth/login", "/health", "/health/ready"}) {
    EXPECT_EQ(BuildContentSecurityPolicy(path), strict) << path;
  }
}

}  // namespace
}  // namespace lan_file_hub
