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

}  // namespace
}  // namespace lan_file_hub
