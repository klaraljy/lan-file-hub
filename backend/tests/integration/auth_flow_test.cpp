// 认证流程集成测试：需要真实 PostgreSQL。
//
// 数据库不可用、或 users 表还没建时，测试会明确跳过（GTEST_SKIP）而不是假装通过。
// 设置环境变量 LAN_FILE_HUB_REQUIRE_DB=1 可让缺失的数据库变成失败（CI 用）。
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "lan_file_hub/application/auth_service.h"
#include "lan_file_hub/common/config.h"
#include "lan_file_hub/infrastructure/database.h"
#include "lan_file_hub/infrastructure/jwt_service.h"
#include "lan_file_hub/infrastructure/password_hasher.h"
#include "lan_file_hub/infrastructure/user_repository.h"

namespace lan_file_hub {
namespace {

constexpr char kSecret[] = "integration-test-secret-0123456789abcdefghij";

bool DatabaseRequired() {
#if defined(_MSC_VER)
  // MSVC 把 std::getenv 标为不安全（C4996），这里用它的安全版本
  char* buffer = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&buffer, &size, "LAN_FILE_HUB_REQUIRE_DB") != 0 || buffer == nullptr) {
    return false;
  }
  const std::string value(buffer);
  std::free(buffer);
  return value == "1";
#else
  const char* value = std::getenv("LAN_FILE_HUB_REQUIRE_DB");
  return value != nullptr && std::string(value) == "1";
#endif
}

// 每个测试用例建一个独立连接池；数据库不可用时返回 nullptr
std::shared_ptr<ConnectionPool> TryConnect() {
  auto config = LoadConfig(".env");
  if (!config.Ok())
    return nullptr;
  auto pool = ConnectionPool::Create(config.Value().config.database, 2);
  if (!pool)
    return nullptr;
  return pool.Value();
}

Argon2Options FastOptions() {
  Argon2Options options;
  options.memory_cost_kib = 1024;
  options.time_cost = 1;
  return options;
}

// 测试夹具：连不上库或表不存在就跳过
class AuthFlowTest : public ::testing::Test {
 protected:
  void SetUp() override {
    pool_ = TryConnect();
    if (!pool_) {
      if (DatabaseRequired()) {
        FAIL() << "数据库不可用，但设置了 LAN_FILE_HUB_REQUIRE_DB=1";
      }
      GTEST_SKIP() << "跳过：PostgreSQL 不可用（先 docker compose up -d postgres 并跑迁移）";
    }

    users_ = std::make_shared<UserRepository>(*pool_);
    hasher_ = std::make_shared<PasswordHasher>(FastOptions());
    jwt_ = std::make_shared<JwtService>(JwtOptions{kSecret, 3600, "lan-file-hub"});
    auth_ = std::make_shared<AuthService>(*users_, *hasher_, *jwt_,
                                          AuthServiceOptions{1024ULL * 1024 * 1024});

    // 表不存在（没跑迁移）时也跳过，并给出可操作的提示
    auto cleared = users_->DeleteAllForTesting();
    if (!cleared.Ok()) {
      if (DatabaseRequired()) {
        FAIL() << "users 表不可用：" << cleared.GetError().message
               << "（先运行 lan_file_hub_migrate）";
      }
      GTEST_SKIP() << "跳过：users 表不存在，请先运行 lan_file_hub_migrate";
    }
  }

  void TearDown() override {
    if (users_) {
      (void)users_->DeleteAllForTesting();
    }
  }

  RegisterRequest ValidRegister(const std::string& username = "alice") {
    RegisterRequest request;
    request.username = username;
    request.email = username + "@example.com";
    request.password = "correct-horse-battery";
    return request;
  }

  std::shared_ptr<ConnectionPool> pool_;
  std::shared_ptr<UserRepository> users_;
  std::shared_ptr<PasswordHasher> hasher_;
  std::shared_ptr<JwtService> jwt_;
  std::shared_ptr<AuthService> auth_;
};

TEST_F(AuthFlowTest, RegistersUserAndReturnsUsableToken) {
  const auto result = auth_->Register(ValidRegister());
  ASSERT_TRUE(result.Ok()) << result.GetError().message;

  EXPECT_FALSE(result.Value().access_token.empty());
  EXPECT_EQ(result.Value().expires_in_seconds, 3600);
  EXPECT_EQ(result.Value().user.username, "alice");
  EXPECT_EQ(result.Value().user.role, UserRole::kUser);
  EXPECT_EQ(result.Value().user.status, UserStatus::kActive);
  EXPECT_FALSE(result.Value().user.id.empty());

  // 签发的 Token 必须能验回来，并且 /me 用得到
  const auto claims = jwt_->Verify(result.Value().access_token);
  ASSERT_TRUE(claims.Ok());
  EXPECT_EQ(claims.Value().user_id, result.Value().user.id);
}

TEST_F(AuthFlowTest, NeverPersistsOrReturnsPlaintextPassword) {
  const auto result = auth_->Register(ValidRegister());
  ASSERT_TRUE(result.Ok());

  const auto stored = users_->FindById(result.Value().user.id);
  ASSERT_TRUE(stored.Ok());
  ASSERT_TRUE(stored.Value().has_value());
  EXPECT_EQ(stored.Value()->password_hash.find("correct-horse-battery"), std::string::npos);
  EXPECT_EQ(stored.Value()->password_hash.rfind("$argon2id$", 0), 0U);
}

TEST_F(AuthFlowTest, RejectsDuplicateUsername) {
  ASSERT_TRUE(auth_->Register(ValidRegister()).Ok());

  RegisterRequest duplicate = ValidRegister();
  duplicate.email = "another@example.com";
  const auto second = auth_->Register(duplicate);
  ASSERT_FALSE(second.Ok());
  EXPECT_EQ(second.GetError().code, "RESOURCE_CONFLICT");
}

TEST_F(AuthFlowTest, RejectsDuplicateEmailRegardlessOfCase) {
  ASSERT_TRUE(auth_->Register(ValidRegister()).Ok());

  RegisterRequest duplicate = ValidRegister("bob");
  duplicate.email = "ALICE@EXAMPLE.COM";
  const auto second = auth_->Register(duplicate);
  ASSERT_FALSE(second.Ok());
  EXPECT_EQ(second.GetError().code, "RESOURCE_CONFLICT");
}

TEST_F(AuthFlowTest, LogsInWithUsernameOrEmailAndRejectsWrongPassword) {
  ASSERT_TRUE(auth_->Register(ValidRegister()).Ok());

  LoginRequest by_username;
  by_username.identifier = "alice";
  by_username.password = "correct-horse-battery";
  EXPECT_TRUE(auth_->Login(by_username).Ok());

  LoginRequest by_email;
  by_email.identifier = "Alice@Example.com";  // 大小写不敏感
  by_email.password = "correct-horse-battery";
  EXPECT_TRUE(auth_->Login(by_email).Ok());

  LoginRequest wrong;
  wrong.identifier = "alice";
  wrong.password = "definitely-not-it";
  const auto failed = auth_->Login(wrong);
  ASSERT_FALSE(failed.Ok());
  EXPECT_EQ(failed.GetError().code, "UNAUTHENTICATED");
}

TEST_F(AuthFlowTest, UnknownUserAndWrongPasswordLookIdentical) {
  ASSERT_TRUE(auth_->Register(ValidRegister()).Ok());

  LoginRequest wrong_password;
  wrong_password.identifier = "alice";
  wrong_password.password = "definitely-not-it";

  LoginRequest unknown_user;
  unknown_user.identifier = "nobody-here";
  unknown_user.password = "definitely-not-it";

  const auto a = auth_->Login(wrong_password);
  const auto b = auth_->Login(unknown_user);
  ASSERT_FALSE(a.Ok());
  ASSERT_FALSE(b.Ok());
  // 错误码与文案必须完全一致，否则可以用来枚举账号
  EXPECT_EQ(a.GetError().code, b.GetError().code);
  EXPECT_EQ(a.GetError().message, b.GetError().message);
}

TEST_F(AuthFlowTest, DisabledUserCannotLoginOrUseExistingToken) {
  const auto registered = auth_->Register(ValidRegister());
  ASSERT_TRUE(registered.Ok());
  const std::string user_id = registered.Value().user.id;
  const std::string token = registered.Value().access_token;

  ASSERT_TRUE(users_->UpdateStatus(user_id, UserStatus::kDisabled).Ok());

  LoginRequest login;
  login.identifier = "alice";
  login.password = "correct-horse-battery";
  const auto denied = auth_->Login(login);
  ASSERT_FALSE(denied.Ok());
  EXPECT_EQ(denied.GetError().code, "FORBIDDEN");

  // 关键：Token 还没过期，但被禁用后不能再访问受保护资源
  const auto current = auth_->CurrentUser(user_id);
  ASSERT_FALSE(current.Ok());
  EXPECT_EQ(current.GetError().code, "FORBIDDEN");
  EXPECT_FALSE(token.empty());
}

TEST_F(AuthFlowTest, CurrentUserReturnsTheAuthenticatedUser) {
  const auto registered = auth_->Register(ValidRegister());
  ASSERT_TRUE(registered.Ok());

  const auto current = auth_->CurrentUser(registered.Value().user.id);
  ASSERT_TRUE(current.Ok());
  EXPECT_EQ(current.Value().username, "alice");
  EXPECT_EQ(current.Value().id, registered.Value().user.id);
}

TEST_F(AuthFlowTest, CurrentUserRejectsUnknownId) {
  const auto current = auth_->CurrentUser("b730a85f-4543-40c9-ab02-f51cae8729ab");
  ASSERT_FALSE(current.Ok());
  EXPECT_EQ(current.GetError().code, "UNAUTHENTICATED");
}

TEST_F(AuthFlowTest, RegisteredUserGetsConfiguredQuota) {
  const auto registered = auth_->Register(ValidRegister());
  ASSERT_TRUE(registered.Ok());
  EXPECT_EQ(registered.Value().user.storage_quota_bytes, 1024ULL * 1024 * 1024);
  EXPECT_EQ(registered.Value().user.used_storage_bytes, 0U);
}

}  // namespace
}  // namespace lan_file_hub
