// 文件夹流程集成测试：需要真实 PostgreSQL。
// 数据库不可用或表不存在时明确跳过（GTEST_SKIP），设置 LAN_FILE_HUB_REQUIRE_DB=1 则失败。
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "lan_file_hub/application/folder_service.h"
#include "lan_file_hub/application/permission_checker.h"
#include "lan_file_hub/common/config.h"
#include "lan_file_hub/common/pagination.h"
#include "lan_file_hub/common/uuid.h"
#include "lan_file_hub/infrastructure/database.h"
#include "lan_file_hub/infrastructure/folder_repository.h"
#include "lan_file_hub/infrastructure/user_repository.h"

namespace lan_file_hub {
namespace {

bool DatabaseRequired() {
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

class FolderFlowTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto config = LoadConfig(".env");
    if (!config.Ok()) {
      Skip("配置不可用");
      return;
    }
    auto pool = ConnectionPool::Create(config.Value().config.database, 2);
    if (!pool) {
      Skip("PostgreSQL 不可用（先 docker compose up -d postgres 并跑迁移）");
      return;
    }
    pool_ = pool.Value();
    users_ = std::make_shared<UserRepository>(*pool_);
    folders_ = std::make_shared<FolderRepository>(*pool_);
    permissions_ = std::make_shared<OwnerBasedPermissionChecker>();
    service_ = std::make_shared<FolderService>(*folders_, *permissions_);

    // 清空 users（folders 通过外键级联删除）
    if (!users_->DeleteAllForTesting().Ok()) {
      Skip("users 表不可用，请先运行 lan_file_hub_migrate");
      return;
    }

    owner_ = MakeUser("owner");
    other_ = MakeUser("other");
    ASSERT_TRUE(users_->Create(owner_).Ok());
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
    user.storage_quota_bytes = 1024ULL * 1024 * 1024;
    return user;
  }

  Actor Owner() const { return Actor{owner_.id, owner_.role}; }
  Actor Other() const { return Actor{other_.id, other_.role}; }

  Result<Folder> Root(const Actor& actor) { return service_->EnsureRootFolder(actor); }

  Result<Folder> MakeFolder(const Actor& actor, const std::string& parent_id,
                            const std::string& name) {
    return service_->Create(actor, parent_id, name);
  }

  std::shared_ptr<ConnectionPool> pool_;
  std::shared_ptr<UserRepository> users_;
  std::shared_ptr<FolderRepository> folders_;
  std::shared_ptr<OwnerBasedPermissionChecker> permissions_;
  std::shared_ptr<FolderService> service_;
  User owner_;
  User other_;
};

TEST_F(FolderFlowTest, EnsuresExactlyOneRootPerUserAndItIsIdempotent) {
  const auto first = Root(Owner());
  ASSERT_TRUE(first.Ok()) << first.GetError().message;
  EXPECT_TRUE(first.Value().IsRoot());
  EXPECT_TRUE(first.Value().name.empty());

  const auto second = Root(Owner());
  ASSERT_TRUE(second.Ok());
  EXPECT_EQ(second.Value().id, first.Value().id) << "重复调用必须返回同一个根目录";

  const auto other_root = Root(Other());
  ASSERT_TRUE(other_root.Ok());
  EXPECT_NE(other_root.Value().id, first.Value().id) << "不同用户各有自己的根目录";
}

TEST_F(FolderFlowTest, CreatesChildUnderRootWithCorrectPath) {
  const auto root = Root(Owner());
  ASSERT_TRUE(root.Ok());

  const auto created = MakeFolder(Owner(), root.Value().id, "documents");
  ASSERT_TRUE(created.Ok()) << created.GetError().message;
  EXPECT_EQ(created.Value().name, "documents");
  EXPECT_EQ(created.Value().parent_id.value_or(""), root.Value().id);
  // 子目录的 path_key = 父 path_key + 自身 id + "/"
  EXPECT_EQ(created.Value().path_key, root.Value().path_key + created.Value().id + "/");
}

TEST_F(FolderFlowTest, RejectsDuplicateNameInSameParentRegardlessOfCase) {
  const auto root = Root(Owner());
  ASSERT_TRUE(root.Ok());

  ASSERT_TRUE(MakeFolder(Owner(), root.Value().id, "Documents").Ok());
  const auto duplicate = MakeFolder(Owner(), root.Value().id, "documents");
  ASSERT_FALSE(duplicate.Ok());
  EXPECT_EQ(duplicate.GetError().code, "RESOURCE_CONFLICT");
}

TEST_F(FolderFlowTest, AllowsSameNameUnderDifferentParents) {
  const auto root = Root(Owner());
  ASSERT_TRUE(root.Ok());
  const auto a = MakeFolder(Owner(), root.Value().id, "a");
  const auto b = MakeFolder(Owner(), root.Value().id, "b");
  ASSERT_TRUE(a.Ok());
  ASSERT_TRUE(b.Ok());

  EXPECT_TRUE(MakeFolder(Owner(), a.Value().id, "shared").Ok());
  EXPECT_TRUE(MakeFolder(Owner(), b.Value().id, "shared").Ok());
}

TEST_F(FolderFlowTest, ListsOnlyActiveChildrenWithPagination) {
  const auto root = Root(Owner());
  ASSERT_TRUE(root.Ok());
  for (const char* name : {"alpha", "beta", "gamma"}) {
    ASSERT_TRUE(MakeFolder(Owner(), root.Value().id, name).Ok());
  }

  Pagination pagination;
  pagination.page = 1;
  pagination.page_size = 2;
  const auto page1 = service_->ListChildren(Owner(), root.Value().id, pagination);
  ASSERT_TRUE(page1.Ok());
  EXPECT_EQ(page1.Value().total, 3U);
  EXPECT_EQ(page1.Value().folders.size(), 2U);
  EXPECT_EQ(page1.Value().folders[0].name, "alpha");

  pagination.page = 2;
  const auto page2 = service_->ListChildren(Owner(), root.Value().id, pagination);
  ASSERT_TRUE(page2.Ok());
  EXPECT_EQ(page2.Value().folders.size(), 1U);
  EXPECT_EQ(page2.Value().folders[0].name, "gamma");
}

TEST_F(FolderFlowTest, RenamesFolderAndRejectsRootRename) {
  const auto root = Root(Owner());
  ASSERT_TRUE(root.Ok());
  const auto folder = MakeFolder(Owner(), root.Value().id, "old-name");
  ASSERT_TRUE(folder.Ok());

  const auto renamed = service_->Rename(Owner(), folder.Value().id, "new-name");
  ASSERT_TRUE(renamed.Ok()) << renamed.GetError().message;
  EXPECT_EQ(renamed.Value().name, "new-name");

  const auto root_rename = service_->Rename(Owner(), root.Value().id, "whatever");
  ASSERT_FALSE(root_rename.Ok());
  EXPECT_EQ(root_rename.GetError().code, "FORBIDDEN");
}

TEST_F(FolderFlowTest, MovesFolderAndRewritesSubtreePaths) {
  const auto root = Root(Owner());
  ASSERT_TRUE(root.Ok());
  const auto source = MakeFolder(Owner(), root.Value().id, "source");
  const auto target = MakeFolder(Owner(), root.Value().id, "target");
  ASSERT_TRUE(source.Ok());
  ASSERT_TRUE(target.Ok());
  const auto child = MakeFolder(Owner(), source.Value().id, "inner");
  ASSERT_TRUE(child.Ok());

  const auto moved = service_->Move(Owner(), source.Value().id, target.Value().id);
  ASSERT_TRUE(moved.Ok()) << moved.GetError().message;
  EXPECT_EQ(moved.Value().parent_id.value_or(""), target.Value().id);
  EXPECT_EQ(moved.Value().path_key, target.Value().path_key + source.Value().id + "/");

  // 子目录的 path_key 必须跟着重写，否则子树查询会失效
  const auto reloaded_child = folders_->FindById(child.Value().id);
  ASSERT_TRUE(reloaded_child.Ok());
  ASSERT_TRUE(reloaded_child.Value().has_value());
  EXPECT_EQ(reloaded_child.Value()->path_key, moved.Value().path_key + child.Value().id + "/");
}

TEST_F(FolderFlowTest, RefusesMoveIntoOwnSubtreeOrSelf) {
  const auto root = Root(Owner());
  ASSERT_TRUE(root.Ok());
  const auto parent = MakeFolder(Owner(), root.Value().id, "parent");
  ASSERT_TRUE(parent.Ok());
  const auto child = MakeFolder(Owner(), parent.Value().id, "child");
  ASSERT_TRUE(child.Ok());

  const auto into_subtree = service_->Move(Owner(), parent.Value().id, child.Value().id);
  ASSERT_FALSE(into_subtree.Ok());
  EXPECT_EQ(into_subtree.GetError().code, "INVALID_ARGUMENT");

  const auto into_self = service_->Move(Owner(), parent.Value().id, parent.Value().id);
  ASSERT_FALSE(into_self.Ok());
  EXPECT_EQ(into_self.GetError().code, "INVALID_ARGUMENT");

  const auto root_move = service_->Move(Owner(), root.Value().id, parent.Value().id);
  ASSERT_FALSE(root_move.Ok());
  EXPECT_EQ(root_move.GetError().code, "FORBIDDEN");
}

TEST_F(FolderFlowTest, SoftDeleteHidesSubtreeFromNormalListing) {
  const auto root = Root(Owner());
  ASSERT_TRUE(root.Ok());
  const auto parent = MakeFolder(Owner(), root.Value().id, "parent");
  ASSERT_TRUE(parent.Ok());
  const auto child = MakeFolder(Owner(), parent.Value().id, "child");
  ASSERT_TRUE(child.Ok());

  ASSERT_TRUE(service_->SoftDelete(Owner(), parent.Value().id).Ok());

  Pagination pagination;
  const auto children = service_->ListChildren(Owner(), root.Value().id, pagination);
  ASSERT_TRUE(children.Ok());
  EXPECT_EQ(children.Value().total, 0U) << "软删除的目录不得出现在普通列表里";

  // 已删除的目录等同于不存在
  const auto gone = service_->Get(Owner(), parent.Value().id);
  ASSERT_FALSE(gone.Ok());
  EXPECT_EQ(gone.GetError().code, "RESOURCE_NOT_FOUND");
  const auto gone_child = service_->Get(Owner(), child.Value().id);
  ASSERT_FALSE(gone_child.Ok());

  // 数据库里仍然是 TRASHED 而不是被物理删除
  const auto raw = folders_->FindById(parent.Value().id);
  ASSERT_TRUE(raw.Ok());
  ASSERT_TRUE(raw.Value().has_value());
  EXPECT_EQ(raw.Value()->status, FolderStatus::kTrashed);
  EXPECT_TRUE(raw.Value()->deleted_at.has_value());
}

TEST_F(FolderFlowTest, RefusesRootDeletion) {
  const auto root = Root(Owner());
  ASSERT_TRUE(root.Ok());
  const auto status = service_->SoftDelete(Owner(), root.Value().id);
  ASSERT_FALSE(status.Ok());
  EXPECT_EQ(status.GetError().code, "FORBIDDEN");
}

TEST_F(FolderFlowTest, OtherUserCannotSeeOrTouchForeignFolders) {
  const auto root = Root(Owner());
  ASSERT_TRUE(root.Ok());
  const auto folder = MakeFolder(Owner(), root.Value().id, "private");
  ASSERT_TRUE(folder.Ok());

  // 对外一律表现为「不存在」，不泄露资源是否存在
  const auto read = service_->Get(Other(), folder.Value().id);
  ASSERT_FALSE(read.Ok());
  EXPECT_EQ(read.GetError().code, "RESOURCE_NOT_FOUND");

  const auto rename = service_->Rename(Other(), folder.Value().id, "hijacked");
  ASSERT_FALSE(rename.Ok());
  EXPECT_EQ(rename.GetError().code, "RESOURCE_NOT_FOUND");

  const auto remove = service_->SoftDelete(Other(), folder.Value().id);
  ASSERT_FALSE(remove.Ok());
  EXPECT_EQ(remove.GetError().code, "RESOURCE_NOT_FOUND");

  // 别人的根目录也不能拿来当父目录
  const auto other_root = Root(Other());
  ASSERT_TRUE(other_root.Ok());
  const auto planted = MakeFolder(Owner(), other_root.Value().id, "planted");
  ASSERT_FALSE(planted.Ok());
  EXPECT_EQ(planted.GetError().code, "RESOURCE_NOT_FOUND");
}

TEST_F(FolderFlowTest, AdminCanAccessForeignFolders) {
  const auto root = Root(Owner());
  ASSERT_TRUE(root.Ok());
  const auto folder = MakeFolder(Owner(), root.Value().id, "shared-by-admin");
  ASSERT_TRUE(folder.Ok());

  Actor admin{other_.id, UserRole::kAdmin};
  const auto read = service_->Get(admin, folder.Value().id);
  EXPECT_TRUE(read.Ok()) << "管理员可以访问（调用方需另行记录审计）";
}

TEST_F(FolderFlowTest, MalformedIdsAreClientErrorsNotServerErrors) {
  // 非 UUID 文本会让 PostgreSQL 抛 22P02（invalid_text_representation）。
  // 这类客户端参数问题必须是 INVALID_ARGUMENT（400），不能被兜底成
  // STORAGE_UNAVAILABLE（503）——那会把客户端错误伪装成"服务不可用、可以重试"。
  const auto created = MakeFolder(Owner(), "not-a-uuid", "documents");
  ASSERT_FALSE(created.Ok());
  EXPECT_EQ(created.GetError().code, "INVALID_ARGUMENT");

  const auto empty_parent = MakeFolder(Owner(), "", "documents");
  ASSERT_FALSE(empty_parent.Ok());
  EXPECT_EQ(empty_parent.GetError().code, "INVALID_ARGUMENT");

  const auto fetched = service_->Get(Owner(), "still-not-a-uuid");
  ASSERT_FALSE(fetched.Ok());
  EXPECT_EQ(fetched.GetError().code, "INVALID_ARGUMENT");
}

}  // namespace
}  // namespace lan_file_hub
