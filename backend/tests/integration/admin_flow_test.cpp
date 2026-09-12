// 管理员接口集成测试（阶段十三第 13～18 条）。打真实 PostgreSQL，连不上时跳过。
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "lan_file_hub/application/admin_service.h"
#include "lan_file_hub/application/folder_service.h"
#include "lan_file_hub/common/config.h"
#include "lan_file_hub/common/pagination.h"
#include "lan_file_hub/common/sha256.h"
#include "lan_file_hub/common/uuid.h"
#include "lan_file_hub/infrastructure/audit_repository.h"
#include "lan_file_hub/infrastructure/database.h"
#include "lan_file_hub/infrastructure/file_repository.h"
#include "lan_file_hub/infrastructure/folder_repository.h"
#include "lan_file_hub/infrastructure/upload_session_repository.h"
#include "lan_file_hub/infrastructure/user_repository.h"
#include "lan_file_hub/storage/local_object_storage.h"

#include "fixtures/temp_dir.h"

namespace lan_file_hub {
namespace {

constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;

bool DatabaseRequired() {
  // 与其它集成测试一致：MSVC 下用 _dupenv_s，避免 getenv 的弃用告警（本项目 -Werror）
  char* buffer = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&buffer, &size, "LAN_FILE_HUB_REQUIRE_DB") != 0 || buffer == nullptr) {
    return false;
  }
  const std::string value(buffer);
  std::free(buffer);
  return value == "1";
}

class AdminFlowTest : public ::testing::Test {
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
    files_ = std::make_shared<FileRepository>(*pool_);
    folders_ = std::make_shared<FolderRepository>(*pool_);
    sessions_ = std::make_shared<UploadSessionRepository>(*pool_);
    audit_ = std::make_shared<AuditLogRepository>(*pool_);

    if (!users_->DeleteAllForTesting().Ok()) {
      Skip("users 表不可用，请先运行 lan_file_hub_migrate");
      return;
    }
    (void)sessions_->DeleteAllForTesting();
    (void)files_->DeleteAllForTesting();
    (void)audit_->DeleteAllForTesting();

    auto storage = LocalObjectStorage::Create(storage_dir_.Path());
    ASSERT_TRUE(storage.Ok()) << storage.GetError().message;
    storage_ = storage.Value();

    admin_service_ =
        std::make_shared<AdminService>(*users_, *files_, *audit_, *sessions_, *storage_);

    admin_ = MakeUser("adminuser");
    admin_.role = UserRole::kAdmin;
    normal_ = MakeUser("normaluser");
    other_ = MakeUser("otheruser");
    ASSERT_TRUE(users_->Create(admin_).Ok());
    ASSERT_TRUE(users_->Create(normal_).Ok());
    ASSERT_TRUE(users_->Create(other_).Ok());
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

  User MakeUser(const std::string& name) {
    User user;
    user.id = GenerateUuidV4();
    user.username = name;
    user.email = name + "@example.com";
    user.password_hash = "$argon2id$v=19$m=1024,t=1,p=1$c2FsdA$aGFzaA";
    user.role = UserRole::kUser;
    user.status = UserStatus::kActive;
    user.storage_quota_bytes = 1024ULL * kMiB;
    user.used_storage_bytes = 0;
    return user;
  }

  Actor Admin() const { return Actor{admin_.id, UserRole::kAdmin}; }
  Actor Normal() const { return Actor{normal_.id, UserRole::kUser}; }

  Pagination Page(int page = 1, int page_size = 20) {
    Pagination pagination;
    pagination.page = page;
    pagination.page_size = page_size;
    return pagination;
  }

  testing::TempDir storage_dir_{"admin_storage"};
  std::shared_ptr<ConnectionPool> pool_;
  std::shared_ptr<UserRepository> users_;
  std::shared_ptr<FileRepository> files_;
  std::shared_ptr<FolderRepository> folders_;
  std::shared_ptr<UploadSessionRepository> sessions_;
  std::shared_ptr<AuditLogRepository> audit_;
  std::shared_ptr<LocalObjectStorage> storage_;
  std::shared_ptr<AdminService> admin_service_;
  User admin_;
  User normal_;
  User other_;
};

TEST_F(AdminFlowTest, NonAdminIsRejectedOnEveryAdminEntryPoint) {
  const auto listed = admin_service_->ListUsers(Normal(), "", "", "username", false, Page());
  ASSERT_FALSE(listed.Ok());
  EXPECT_EQ(listed.GetError().code, "FORBIDDEN");

  const auto stats = admin_service_->StorageStats(Normal());
  ASSERT_FALSE(stats.Ok());
  EXPECT_EQ(stats.GetError().code, "FORBIDDEN");

  const auto logs = admin_service_->QueryAuditLogs(Normal(), AuditLogFilter{}, Page());
  ASSERT_FALSE(logs.Ok());
  EXPECT_EQ(logs.GetError().code, "FORBIDDEN");

  const auto status = admin_service_->SetUserStatus(Normal(), other_.id, UserStatus::kDisabled);
  ASSERT_FALSE(status.Ok());
  EXPECT_EQ(status.GetError().code, "FORBIDDEN");

  const auto quota = admin_service_->SetUserQuota(Normal(), other_.id, 2048ULL * kMiB);
  ASSERT_FALSE(quota.Ok());
  EXPECT_EQ(quota.GetError().code, "FORBIDDEN");

  // 被拒的调用不得改变任何状态
  auto target = users_->FindById(other_.id);
  ASSERT_TRUE(target.Ok());
  ASSERT_TRUE(target.Value().has_value());
  EXPECT_EQ(target.Value()->status, UserStatus::kActive);
  EXPECT_EQ(target.Value()->storage_quota_bytes, 1024ULL * kMiB);
}

TEST_F(AdminFlowTest, AdminListsUsersWithSearchAndPagination) {
  const auto all = admin_service_->ListUsers(Admin(), "", "", "username", false, Page(1, 2));
  ASSERT_TRUE(all.Ok()) << all.GetError().message;
  EXPECT_EQ(all.Value().users.size(), 2U) << "分页每页 2 条";
  EXPECT_EQ(all.Value().total, 3U);

  const auto searched =
      admin_service_->ListUsers(Admin(), "otheruser", "", "username", false, Page());
  ASSERT_TRUE(searched.Ok());
  ASSERT_EQ(searched.Value().users.size(), 1U);
  EXPECT_EQ(searched.Value().users[0].username, "otheruser");
  EXPECT_EQ(searched.Value().total, 1U);
}

TEST_F(AdminFlowTest, UnknownSortFieldIsRejectedInsteadOfSilentlyIgnored) {
  const auto listed = admin_service_->ListUsers(Admin(), "", "", "password_hash", false, Page());
  ASSERT_FALSE(listed.Ok());
  EXPECT_EQ(listed.GetError().code, "INVALID_ARGUMENT");
}

TEST_F(AdminFlowTest, SearchWildcardsAreEscaped) {
  // '%' 若被当成通配符，这个搜索会返回全部用户
  const auto searched = admin_service_->ListUsers(Admin(), "%", "", "username", false, Page());
  ASSERT_TRUE(searched.Ok());
  EXPECT_EQ(searched.Value().total, 0U) << "LIKE 通配符必须被转义";
}

TEST_F(AdminFlowTest, AdminDisablesAndEnablesUserWithAuditTrail) {
  ASSERT_TRUE(admin_service_->SetUserStatus(Admin(), other_.id, UserStatus::kDisabled).Ok());

  auto after_disable = users_->FindById(other_.id);
  ASSERT_TRUE(after_disable.Ok());
  ASSERT_TRUE(after_disable.Value().has_value());
  EXPECT_EQ(after_disable.Value()->status, UserStatus::kDisabled);

  auto audit_count = audit_->CountByAction("admin_disable_user");
  ASSERT_TRUE(audit_count.Ok());
  EXPECT_EQ(audit_count.Value(), 1U) << "管理员操作必须留痕";

  // 幂等：重复禁用不报错、不再多写一条审计
  ASSERT_TRUE(admin_service_->SetUserStatus(Admin(), other_.id, UserStatus::kDisabled).Ok());
  auto audit_again = audit_->CountByAction("admin_disable_user");
  ASSERT_TRUE(audit_again.Ok());
  EXPECT_EQ(audit_again.Value(), 1U);

  ASSERT_TRUE(admin_service_->SetUserStatus(Admin(), other_.id, UserStatus::kActive).Ok());
  auto re_enabled = users_->FindById(other_.id);
  ASSERT_TRUE(re_enabled.Ok());
  ASSERT_TRUE(re_enabled.Value().has_value());
  EXPECT_EQ(re_enabled.Value()->status, UserStatus::kActive);
  auto enable_audit = audit_->CountByAction("admin_enable_user");
  ASSERT_TRUE(enable_audit.Ok());
  EXPECT_EQ(enable_audit.Value(), 1U);
}

TEST_F(AdminFlowTest, AdminCannotDisableSelf) {
  const auto result = admin_service_->SetUserStatus(Admin(), admin_.id, UserStatus::kDisabled);
  ASSERT_FALSE(result.Ok());
  EXPECT_EQ(result.GetError().code, "INVALID_ARGUMENT");

  auto self = users_->FindById(admin_.id);
  ASSERT_TRUE(self.Ok());
  ASSERT_TRUE(self.Value().has_value());
  EXPECT_EQ(self.Value()->status, UserStatus::kActive) << "管理员不能把自己锁在门外";
}

TEST_F(AdminFlowTest, SetStatusOnMissingUserIsNotFound) {
  const auto result =
      admin_service_->SetUserStatus(Admin(), GenerateUuidV4(), UserStatus::kDisabled);
  ASSERT_FALSE(result.Ok());
  EXPECT_EQ(result.GetError().code, "RESOURCE_NOT_FOUND");
}

TEST_F(AdminFlowTest, AdminChangesQuotaWithAuditTrail) {
  ASSERT_TRUE(admin_service_->SetUserQuota(Admin(), other_.id, 4096ULL * kMiB).Ok());

  auto target = users_->FindById(other_.id);
  ASSERT_TRUE(target.Ok());
  ASSERT_TRUE(target.Value().has_value());
  EXPECT_EQ(target.Value()->storage_quota_bytes, 4096ULL * kMiB);

  auto audit_count = audit_->CountByAction("admin_set_quota");
  ASSERT_TRUE(audit_count.Ok());
  EXPECT_EQ(audit_count.Value(), 1U);

  const auto zero = admin_service_->SetUserQuota(Admin(), other_.id, 0);
  ASSERT_FALSE(zero.Ok());
  EXPECT_EQ(zero.GetError().code, "INVALID_ARGUMENT");
}

TEST_F(AdminFlowTest, StorageStatsReturnsAggregates) {
  const auto stats = admin_service_->StorageStats(Admin());
  ASSERT_TRUE(stats.Ok()) << stats.GetError().message;
  EXPECT_EQ(stats.Value().user_count, 3U);
  EXPECT_EQ(stats.Value().file_count, 0U);
  EXPECT_EQ(stats.Value().version_count, 0U);
  EXPECT_EQ(stats.Value().used_bytes, 0U);
  EXPECT_GT(stats.Value().free_bytes, 0U) << "可用空间必须能读到";
  EXPECT_EQ(stats.Value().active_uploads, 0U);
}

TEST_F(AdminFlowTest, QueryingAuditLogsIsItselfAudited) {
  ASSERT_TRUE(admin_service_->SetUserStatus(Admin(), other_.id, UserStatus::kDisabled).Ok());

  AuditLogFilter filter;
  filter.action = "admin_disable_user";
  const auto page = admin_service_->QueryAuditLogs(Admin(), filter, Page());
  ASSERT_TRUE(page.Ok()) << page.GetError().message;
  EXPECT_EQ(page.Value().total, 1U);
  ASSERT_EQ(page.Value().entries.size(), 1U);
  EXPECT_EQ(page.Value().entries[0].actor_id, admin_.id);

  auto audit_of_audit = audit_->CountByAction("admin_query_audit_logs");
  ASSERT_TRUE(audit_of_audit.Ok());
  EXPECT_EQ(audit_of_audit.Value(), 1U) << "谁翻过审计日志本身也要留痕";
}

TEST_F(AdminFlowTest, AuditQueryFiltersByActorAndResource) {
  ASSERT_TRUE(admin_service_->SetUserQuota(Admin(), other_.id, 2048ULL * kMiB).Ok());
  ASSERT_TRUE(admin_service_->SetUserQuota(Admin(), normal_.id, 3072ULL * kMiB).Ok());

  AuditLogFilter by_actor;
  by_actor.actor_id = admin_.id;
  const auto all_by_admin = admin_service_->QueryAuditLogs(Admin(), by_actor, Page());
  ASSERT_TRUE(all_by_admin.Ok());
  EXPECT_EQ(all_by_admin.Value().total, 2U);

  AuditLogFilter by_resource;
  by_resource.resource_id = other_.id;
  const auto one = admin_service_->QueryAuditLogs(Admin(), by_resource, Page());
  ASSERT_TRUE(one.Ok());
  EXPECT_EQ(one.Value().total, 1U);
  ASSERT_EQ(one.Value().entries.size(), 1U);
  EXPECT_EQ(one.Value().entries[0].resource_id, other_.id);
}

}  // namespace
}  // namespace lan_file_hub
