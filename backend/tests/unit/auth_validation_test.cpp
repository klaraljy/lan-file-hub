#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "lan_file_hub/application/auth_service.h"

// 只测试纯函数部分（输入校验），不碰数据库。
// 需要真实 PostgreSQL 的流程测试在 tests/integration/auth_flow_test.cpp。
namespace lan_file_hub {
namespace {

TEST(AuthValidationTest, AcceptsReasonableUsernames) {
  const std::vector<std::string> names = {"alice", "bob_2026", "a.b-c", "user123", "ABC"};
  for (const auto& name : names) {
    EXPECT_TRUE(AuthService::ValidateUsername(name).Ok()) << "应接受: " << name;
  }
}

TEST(AuthValidationTest, RejectsBadUsernames) {
  const std::vector<std::string> names = {
      "ab",                  // 太短
      std::string(65, 'a'),  // 太长
      "has space",
      "has/slash",
      "中文名",
      "a@b",
  };
  for (const auto& name : names) {
    const auto result = AuthService::ValidateUsername(name);
    EXPECT_FALSE(result.Ok()) << "应拒绝: " << name;
    EXPECT_EQ(result.GetError().code, "INVALID_ARGUMENT");
  }
}

TEST(AuthValidationTest, AcceptsReasonableEmails) {
  const std::vector<std::string> emails = {"alice@example.com", "a.b+c@sub.example.co", "x@y.zz"};
  for (const auto& email : emails) {
    EXPECT_TRUE(AuthService::ValidateEmail(email).Ok()) << "应接受: " << email;
  }
}

TEST(AuthValidationTest, RejectsBadEmails) {
  const std::vector<std::string> emails = {"",      "no-at-sign", "@example.com",
                                           "user@", "user@host",  "has space@example.com"};
  for (const auto& email : emails) {
    const auto result = AuthService::ValidateEmail(email);
    EXPECT_FALSE(result.Ok()) << "应拒绝: " << email;
    EXPECT_EQ(result.GetError().code, "INVALID_ARGUMENT");
  }
}

TEST(AuthValidationTest, RejectsOverlongEmail) {
  const std::string long_email = std::string(250, 'a') + "@example.com";
  EXPECT_FALSE(AuthService::ValidateEmail(long_email).Ok());
}

}  // namespace
}  // namespace lan_file_hub
