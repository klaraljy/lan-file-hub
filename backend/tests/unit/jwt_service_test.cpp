#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "lan_file_hub/infrastructure/jwt_service.h"

namespace lan_file_hub {
namespace {

constexpr char kSecret[] = "unit-test-secret-0123456789abcdefghijklmnop";

User MakeUser() {
  User user;
  user.id = "b730a85f-4543-40c9-ab02-f51cae8729ab";
  user.username = "alice";
  user.role = UserRole::kUser;
  user.status = UserStatus::kActive;
  return user;
}

JwtService MakeService(std::uint32_t expire_seconds = 3600) {
  return JwtService(JwtOptions{kSecret, expire_seconds, "lan-file-hub"});
}

TEST(JwtServiceTest, IssuesTokenThatVerifiesBack) {
  const JwtService service = MakeService();
  const auto token = service.Issue(MakeUser());
  ASSERT_TRUE(token.Ok()) << token.GetError().message;

  // JWT 由三段组成
  EXPECT_EQ(std::count(token.Value().begin(), token.Value().end(), '.'), 2);

  const auto claims = service.Verify(token.Value());
  ASSERT_TRUE(claims.Ok()) << claims.GetError().message;
  EXPECT_EQ(claims.Value().user_id, "b730a85f-4543-40c9-ab02-f51cae8729ab");
  EXPECT_EQ(claims.Value().username, "alice");
  EXPECT_EQ(claims.Value().role, UserRole::kUser);
  EXPECT_GT(claims.Value().expires_at_unix, 0);
}

TEST(JwtServiceTest, CarriesAdminRole) {
  const JwtService service = MakeService();
  User admin = MakeUser();
  admin.role = UserRole::kAdmin;

  const auto token = service.Issue(admin);
  ASSERT_TRUE(token.Ok());
  const auto claims = service.Verify(token.Value());
  ASSERT_TRUE(claims.Ok());
  EXPECT_EQ(claims.Value().role, UserRole::kAdmin);
}

TEST(JwtServiceTest, RejectsTokenSignedWithDifferentSecret) {
  const auto token = MakeService().Issue(MakeUser());
  ASSERT_TRUE(token.Ok());

  const JwtService other(
      JwtOptions{"a-completely-different-secret-value-32bytes", 3600, "lan-file-hub"});
  const auto claims = other.Verify(token.Value());
  ASSERT_FALSE(claims.Ok());
  EXPECT_EQ(claims.GetError().code, "UNAUTHENTICATED");
}

TEST(JwtServiceTest, RejectsTamperedPayload) {
  const auto token = MakeService().Issue(MakeUser());
  ASSERT_TRUE(token.Ok());

  // 改动 payload 段的一个字符，签名应当失配
  std::string tampered = token.Value();
  const auto first_dot = tampered.find('.');
  const auto second_dot = tampered.find('.', first_dot + 1);
  tampered[first_dot + 2] = (tampered[first_dot + 2] == 'a') ? 'b' : 'a';

  const JwtService service = MakeService();
  const auto claims = service.Verify(tampered);
  EXPECT_FALSE(claims.Ok());
  EXPECT_NE(second_dot, std::string::npos);
}

TEST(JwtServiceTest, RejectsGarbageAndEmptyTokens) {
  const JwtService service = MakeService();
  for (const std::string token : {"", "not-a-token", "a.b.c", "....."}) {
    const auto claims = service.Verify(token);
    EXPECT_FALSE(claims.Ok()) << "不应接受: " << token;
    EXPECT_EQ(claims.GetError().code, "UNAUTHENTICATED");
  }
}

TEST(JwtServiceTest, ReturnsTokenExpiredForExpiredToken) {
  const JwtService service = MakeService(1 /* 1 秒有效期 */);
  const auto token = service.Issue(MakeUser());
  ASSERT_TRUE(token.Ok());

  std::this_thread::sleep_for(std::chrono::seconds(2));

  const auto claims = service.Verify(token.Value());
  ASSERT_FALSE(claims.Ok());
  EXPECT_EQ(claims.GetError().code, "TOKEN_EXPIRED")
      << "过期必须与「无效」区分开（阶段二安全要求）";
}

TEST(JwtServiceTest, RefusesToIssueWithoutSecret) {
  const JwtService service(JwtOptions{"", 3600, "lan-file-hub"});
  const auto token = service.Issue(MakeUser());
  ASSERT_FALSE(token.Ok());
  EXPECT_EQ(token.GetError().code, "INTERNAL_ERROR");
}

TEST(JwtServiceTest, ErrorMessagesNeverLeakTheToken) {
  const JwtService service = MakeService();
  const auto claims = service.Verify("eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiJ4In0.forged");
  ASSERT_FALSE(claims.Ok());
  EXPECT_EQ(claims.GetError().message.find("eyJ"), std::string::npos) << "错误信息里不得回显 Token";
  EXPECT_EQ(claims.GetError().detail.find("eyJ"), std::string::npos);
}

}  // namespace
}  // namespace lan_file_hub
