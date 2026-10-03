// 邀请制注册的集成测试（用户 2026-10-02 要求：不是所有人都能注册，要通过管理员邀请）。
//
// 覆盖：只存哈希不存明文、单次/多次邀请的消耗、用完/过期/撤销后不可用、
//       注册失败可以归还次数、参数校验、列表不含 token。
//
// ⚠️ 和其它集成测试一样：**必须跑在独立的测试库上**——SetUp 会清空 users 与 invites。
//    用法见 AGENTS.md §11「集成测试要跑在独立测试库」。
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "lan_file_hub/application/invite_service.h"
#include "lan_file_hub/common/config.h"
#include "lan_file_hub/common/uuid.h"
#include "lan_file_hub/infrastructure/database.h"
#include "lan_file_hub/infrastructure/invite_repository.h"
#include "lan_file_hub/infrastructure/user_repository.h"

namespace lan_file_hub {
namespace {

class InviteFlowTest : public ::testing::Test {
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
    invites_ = std::make_shared<InviteRepository>(*pool_);
    if (!users_->DeleteAllForTesting().Ok() || !invites_->DeleteAllForTesting().Ok()) {
      Skip("表不可用，请先运行 lan_file_hub_migrate（含 0010_invites）");
      return;
    }
    service_ = std::make_shared<InviteService>(*invites_);

    admin_ = MakeUser("invite_admin");
    admin_.role = UserRole::kAdmin;
    ASSERT_TRUE(users_->Create(admin_).Ok());
  }

  void TearDown() override {
    if (invites_) {
      (void)invites_->DeleteAllForTesting();
    }
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

  InviteService::CreateRequest DefaultRequest() {
    InviteService::CreateRequest request;
    request.note = "给小王";
    request.max_uses = 1;
    return request;
  }

  std::shared_ptr<ConnectionPool> pool_;
  std::shared_ptr<UserRepository> users_;
  std::shared_ptr<InviteRepository> invites_;
  std::shared_ptr<InviteService> service_;
  User admin_;
};

TEST_F(InviteFlowTest, StoresOnlyHashNotPlaintext) {
  auto created = service_->Create(DefaultRequest(), admin_.id);
  ASSERT_TRUE(created.Ok()) << created.GetError().message;
  const std::string token = created.Value().token;
  ASSERT_FALSE(token.empty());

  // 用哈希能查到
  auto by_hash = invites_->FindByTokenHash(HashInviteToken(token));
  ASSERT_TRUE(by_hash.Ok()) << by_hash.GetError().message;
  EXPECT_TRUE(by_hash.Value().has_value());

  // 用明文查不到 —— 证明库里没有明文（与分享链接同口径）
  auto by_plain = invites_->FindByTokenHash(token);
  ASSERT_TRUE(by_plain.Ok()) << by_plain.GetError().message;
  EXPECT_FALSE(by_plain.Value().has_value()) << "库里绝不能存 token 明文";
}

TEST_F(InviteFlowTest, SingleUseInviteIsConsumedOnce) {
  auto created = service_->Create(DefaultRequest(), admin_.id);
  ASSERT_TRUE(created.Ok());

  auto first = service_->Consume(created.Value().token);
  ASSERT_TRUE(first.Ok()) << first.GetError().message;
  EXPECT_EQ(first.Value().used_count, 1);

  auto second = service_->Consume(created.Value().token);
  EXPECT_FALSE(second.Ok()) << "同一 token 不能注册第二个人";
  EXPECT_EQ(second.GetError().code, "INVITE_INVALID");
}

TEST_F(InviteFlowTest, MultiUseInviteStopsAtLimit) {
  auto request = DefaultRequest();
  request.max_uses = 2;
  auto created = service_->Create(request, admin_.id);
  ASSERT_TRUE(created.Ok());

  EXPECT_TRUE(service_->Consume(created.Value().token).Ok());
  EXPECT_TRUE(service_->Consume(created.Value().token).Ok());
  auto third = service_->Consume(created.Value().token);
  EXPECT_FALSE(third.Ok()) << "超过 max_uses 必须拒绝";
}

TEST_F(InviteFlowTest, RevokedInviteCannotBeUsed) {
  auto created = service_->Create(DefaultRequest(), admin_.id);
  ASSERT_TRUE(created.Ok());

  ASSERT_TRUE(service_->Revoke(created.Value().invite.id).Ok());
  auto consumed = service_->Consume(created.Value().token);
  EXPECT_FALSE(consumed.Ok());
  EXPECT_EQ(consumed.GetError().code, "INVITE_INVALID");

  auto described = service_->Describe(created.Value().token);
  EXPECT_FALSE(described.Ok());
}

TEST_F(InviteFlowTest, ExpiredInviteCannotBeUsed) {
  // 服务层不允许建"已经过期"的邀请（最短 5 分钟），所以直接用仓储插一条过期的
  auto token = GenerateInviteToken();
  ASSERT_TRUE(token.Ok());
  const std::int64_t past = 1'000'000;  // 1970 年附近，必然已过期
  auto inserted = invites_->Create(HashInviteToken(token.Value()), "过期的", admin_.id, past, 1);
  ASSERT_TRUE(inserted.Ok()) << inserted.GetError().message;

  auto described = service_->Describe(token.Value());
  EXPECT_FALSE(described.Ok()) << "过期邀请必须被拒绝";
  EXPECT_EQ(described.GetError().code, "INVITE_INVALID");
}

TEST_F(InviteFlowTest, ReleaseReturnsConsumedUse) {
  auto created = service_->Create(DefaultRequest(), admin_.id);
  ASSERT_TRUE(created.Ok());

  ASSERT_TRUE(service_->Consume(created.Value().token).Ok());
  // 模拟"注册失败要把次数还回去"
  ASSERT_TRUE(service_->Release(created.Value().token).Ok());

  auto described = service_->Describe(created.Value().token);
  EXPECT_TRUE(described.Ok()) << "归还后这条邀请应该还能用";
  EXPECT_EQ(described.Value().used_count, 0);
}

TEST_F(InviteFlowTest, UnknownTokenIsNotFound) {
  auto described = service_->Describe("this-is-not-a-real-token");
  EXPECT_FALSE(described.Ok());
  EXPECT_EQ(described.GetError().code, "RESOURCE_NOT_FOUND");
}

TEST_F(InviteFlowTest, RejectsInvalidArguments) {
  InviteService::CreateRequest too_many;
  too_many.max_uses = 100000;
  EXPECT_EQ(service_->Create(too_many, admin_.id).GetError().code, "INVALID_ARGUMENT");

  InviteService::CreateRequest zero_uses;
  zero_uses.max_uses = 0;
  EXPECT_EQ(service_->Create(zero_uses, admin_.id).GetError().code, "INVALID_ARGUMENT");

  InviteService::CreateRequest too_short;
  too_short.expires_in_seconds = 10;  // 少于 5 分钟
  EXPECT_EQ(service_->Create(too_short, admin_.id).GetError().code, "INVALID_ARGUMENT");

  InviteService::CreateRequest long_note;
  long_note.note = std::string(InviteService::kMaxNoteLength + 1, 'x');
  EXPECT_EQ(service_->Create(long_note, admin_.id).GetError().code, "INVALID_ARGUMENT");
}

TEST_F(InviteFlowTest, ListReturnsInvitesAndCount) {
  ASSERT_TRUE(service_->Create(DefaultRequest(), admin_.id).Ok());
  auto second = DefaultRequest();
  second.note = "给小李";
  second.max_uses = 5;
  ASSERT_TRUE(service_->Create(second, admin_.id).Ok());

  auto listed = service_->List(20, 0);
  ASSERT_TRUE(listed.Ok()) << listed.GetError().message;
  EXPECT_EQ(listed.Value().size(), 2U);

  auto total = service_->Count();
  ASSERT_TRUE(total.Ok());
  EXPECT_EQ(total.Value(), 2U);
}

}  // namespace
}  // namespace lan_file_hub
