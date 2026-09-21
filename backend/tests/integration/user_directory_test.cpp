// 「发送给某个人」用的用户名录集成测试（用户 2026-09-21 指示，AGENTS.md §14.3）。
//
// 覆盖：按用户名前缀/邮箱找人、排除自己、排除被禁用用户、查询串过短报错、
//       结果只含 id 与 username（不泄露邮箱）、条数上限、大小写不敏感、LIKE 通配符被转义。
//
// ⚠️ 和其它集成测试一样：**必须跑在独立的测试库上**——SetUp 会清空 users 表。
//    用法见 AGENTS.md §11「集成测试要跑在独立测试库」。
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "lan_file_hub/application/user_directory.h"
#include "lan_file_hub/common/config.h"
#include "lan_file_hub/common/uuid.h"
#include "lan_file_hub/infrastructure/database.h"
#include "lan_file_hub/infrastructure/user_repository.h"

namespace lan_file_hub {
namespace {

class UserDirectoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto config = LoadConfig(".env");
    if (!config.Ok()) {
      Skip("配置不可用");
      return;
    }
    auto pool = ConnectionPool::Create(config.Value().config.database, 4);
    if (!pool) {
      Skip("PostgreSQL 不可用（先 docker compose up -d postgres 并跑迁移）");
      return;
    }
    pool_ = pool.Value();
    users_ = std::make_shared<UserRepository>(*pool_);
    if (!users_->DeleteAllForTesting().Ok()) {
      Skip("users 表不可用，请先运行 lan_file_hub_migrate");
      return;
    }

    self_ = MakeUser("directory_self");
    alice_ = MakeUser("directory_alice");
    bob_ = MakeUser("directory_bob");
    disabled_ = MakeUser("directory_disabled");
    disabled_.status = UserStatus::kDisabled;
    ASSERT_TRUE(users_->Create(self_).Ok());
    ASSERT_TRUE(users_->Create(alice_).Ok());
    ASSERT_TRUE(users_->Create(bob_).Ok());
    ASSERT_TRUE(users_->Create(disabled_).Ok());
  }

  void TearDown() override {
    if (users_) {
      (void)users_->DeleteAllForTesting();
    }
  }

  void Skip(const std::string& reason) {
    if (DatabaseRequired()) {
      FAIL() << reason << "（但设置了 LAN_FILE_HUB_REQUIRE_DB=1）";
    }
    GTEST_SKIP() << "跳过：" << reason;
  }

  static bool DatabaseRequired() {
#if defined(_MSC_VER)
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

  static User MakeUser(const std::string& name) {
    User user;
    user.id = GenerateUuidV4();
    user.username = name;
    user.email = name + "@example.com";
    user.password_hash = "$argon2id$v=19$m=1024,t=1,p=1$c2FsdA$aGFzaA";
    user.role = UserRole::kUser;
    user.status = UserStatus::kActive;
    user.storage_quota_bytes = 64ULL * 1024 * 1024;
    return user;
  }

  std::vector<std::string> NamesOf(const std::vector<UserSummary>& summaries) {
    std::vector<std::string> names;
    names.reserve(summaries.size());
    for (const auto& summary : summaries) {
      names.push_back(summary.username);
    }
    return names;
  }

  bool Contains(const std::vector<UserSummary>& summaries, const std::string& username) {
    for (const auto& summary : summaries) {
      if (summary.username == username) {
        return true;
      }
    }
    return false;
  }

  std::shared_ptr<ConnectionPool> pool_;
  std::shared_ptr<UserRepository> users_;
  User self_;
  User alice_;
  User bob_;
  User disabled_;
};

TEST_F(UserDirectoryTest, FindsOtherUserByUsernamePrefix) {
  auto found = SearchUsersForSharing(*users_, "directory_al", self_.id);
  ASSERT_TRUE(found.Ok()) << found.GetError().message;
  EXPECT_TRUE(Contains(found.Value(), alice_.username));
  EXPECT_FALSE(Contains(found.Value(), bob_.username));
}

TEST_F(UserDirectoryTest, FindsUserByEmail) {
  auto found = SearchUsersForSharing(*users_, "directory_bob@example.com", self_.id);
  ASSERT_TRUE(found.Ok()) << found.GetError().message;
  EXPECT_TRUE(Contains(found.Value(), bob_.username));
}

TEST_F(UserDirectoryTest, IsCaseInsensitive) {
  auto found = SearchUsersForSharing(*users_, "DIRECTORY_ALICE", self_.id);
  ASSERT_TRUE(found.Ok()) << found.GetError().message;
  EXPECT_TRUE(Contains(found.Value(), alice_.username));
}

TEST_F(UserDirectoryTest, NeverReturnsSelf) {
  auto found = SearchUsersForSharing(*users_, "directory_", self_.id);
  ASSERT_TRUE(found.Ok()) << found.GetError().message;
  EXPECT_FALSE(Contains(found.Value(), self_.username))
      << "把自己的账号列出来会让用户误以为可以发给自己";
  EXPECT_TRUE(Contains(found.Value(), alice_.username));
}

TEST_F(UserDirectoryTest, SkipsDisabledUsers) {
  auto found = SearchUsersForSharing(*users_, "directory_disabled", self_.id);
  ASSERT_TRUE(found.Ok()) << found.GetError().message;
  EXPECT_TRUE(found.Value().empty()) << "被禁用的用户不该出现在可分享对象里";
}

TEST_F(UserDirectoryTest, RejectsTooShortQuery) {
  auto found = SearchUsersForSharing(*users_, "a", self_.id);
  EXPECT_FALSE(found.Ok());
  EXPECT_EQ(found.GetError().code, "INVALID_ARGUMENT");
}

TEST_F(UserDirectoryTest, TrimsSurroundingWhitespace) {
  auto found = SearchUsersForSharing(*users_, "  directory_alice  ", self_.id);
  ASSERT_TRUE(found.Ok()) << found.GetError().message;
  EXPECT_TRUE(Contains(found.Value(), alice_.username));
}

TEST_F(UserDirectoryTest, EscapesLikeWildcards) {
  // "%" 如果没被转义会匹配到所有人——这里断言它匹配不到任何用户名
  auto found = SearchUsersForSharing(*users_, "%%", self_.id);
  ASSERT_TRUE(found.Ok()) << found.GetError().message;
  EXPECT_TRUE(found.Value().empty()) << "LIKE 通配符必须被转义，否则等于泄露全站用户列表";
}

TEST_F(UserDirectoryTest, ResultContainsOnlyIdAndUsername) {
  auto found = SearchUsersForSharing(*users_, "directory_alice", self_.id);
  ASSERT_TRUE(found.Ok()) << found.GetError().message;
  ASSERT_EQ(found.Value().size(), 1U);
  EXPECT_EQ(found.Value()[0].id, alice_.id);
  EXPECT_EQ(found.Value()[0].username, alice_.username);
}

TEST_F(UserDirectoryTest, HonorsResultLimit) {
  for (int index = 0; index < 15; ++index) {
    const User extra = MakeUser("directory_many_" + std::to_string(index));
    ASSERT_TRUE(users_->Create(extra).Ok());
  }
  auto found = SearchUsersForSharing(*users_, "directory_many_", self_.id);
  ASSERT_TRUE(found.Ok()) << found.GetError().message;
  EXPECT_EQ(found.Value().size(), kMaxUserQueryResults) << "结果条数必须被上限截断";
}

}  // namespace
}  // namespace lan_file_hub
