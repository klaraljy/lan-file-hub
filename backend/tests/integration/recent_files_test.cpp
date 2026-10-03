// 「最近文件」（`GET /api/v1/recent-files`）的集成测试 —— 2026-10-03 补。
//
// 为什么必须有它：这个接口是**跨目录的 SQL 查询**（不走"某个文件夹里有什么"那条路），
// 而且带状态过滤与排序。它此前只有前端单测（137 条，覆盖不到 SQL）与一次手工 HTTP 验证
// （200 / total=1 / 根目录 folder_name 为空），**改坏了不会有人发现**。
//
// 覆盖（对应 docs/next-session-plan.md 里点名要的四条）：
//   ① 排序：按"最后改动时间"倒序，与所在文件夹无关；
//   ② 排除回收站里的文件（软删除后不再出现，总数一起减）；
//   ③ 排除别人的文件（只看自己的，管理员也**不例外**——这个接口是"我的最近文件"）；
//   ④ 分页边界：第 2 页接着第 1 页、不重不漏、每页的 total 都是全量；
//   另外附带：folder_name 要带出来（用户得知道去哪儿找）、空账号返回空列表。
//
// 需要真实 PostgreSQL（状态枚举、排序、分页都在数据库里）。跑法见 AGENTS.md §11：
// ⛔ 必须用独立测试库 lan_file_hub_test（夹具会清空 users/files 等表）。
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <pqxx/pqxx>

#include "fixtures/temp_dir.h"
#include "lan_file_hub/application/file_service.h"
#include "lan_file_hub/application/folder_service.h"
#include "lan_file_hub/application/permission_checker.h"
#include "lan_file_hub/application/trash_service.h"
#include "lan_file_hub/common/config.h"
#include "lan_file_hub/common/pagination.h"
#include "lan_file_hub/common/uuid.h"
#include "lan_file_hub/infrastructure/audit_repository.h"
#include "lan_file_hub/infrastructure/database.h"
#include "lan_file_hub/infrastructure/file_repository.h"
#include "lan_file_hub/infrastructure/folder_repository.h"
#include "lan_file_hub/infrastructure/permission_repository.h"
#include "lan_file_hub/infrastructure/trash_repository.h"
#include "lan_file_hub/infrastructure/user_repository.h"
#include "lan_file_hub/storage/local_object_storage.h"

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

class RecentFilesTest : public ::testing::Test {
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
    folders_ = std::make_shared<FolderRepository>(*pool_);
    files_ = std::make_shared<FileRepository>(*pool_);
    trash_repo_ = std::make_shared<TrashRepository>(*pool_);
    audit_ = std::make_shared<AuditLogRepository>(*pool_);
    cleanup_repo_ = std::make_shared<CleanupTaskRepository>(*pool_);
    permissions_repo_ = std::make_shared<PermissionRepository>(*pool_);
    shares_repo_ = std::make_shared<ShareLinkRepository>(*pool_);
    permissions_ = std::make_shared<OwnerBasedPermissionChecker>();

    if (!users_->DeleteAllForTesting().Ok()) {
      Skip("users 表不可用，请先运行 lan_file_hub_migrate");
      return;
    }
    (void)files_->DeleteAllForTesting();
    (void)audit_->DeleteAllForTesting();
    (void)cleanup_repo_->DeleteAllForTesting();

    auto storage = LocalObjectStorage::Create(storage_dir_.Path());
    ASSERT_TRUE(storage.Ok()) << storage.GetError().message;
    storage_ = storage.Value();

    folders_service_ = std::make_shared<FolderService>(*folders_, *permissions_);
    files_service_ = std::make_shared<FileService>(*files_, *folders_, *storage_, *permissions_,
                                                   FileServiceOptions{64ULL * 1024 * 1024});
    trash_ =
        std::make_shared<TrashService>(*trash_repo_, *files_, *folders_, *audit_, *cleanup_repo_,
                                       *permissions_repo_, *shares_repo_, *permissions_);

    owner_ = MakeUser("recentowner");
    other_ = MakeUser("recentother");
    ASSERT_TRUE(users_->Create(owner_).Ok());
    ASSERT_TRUE(users_->Create(other_).Ok());

    auto root = folders_service_->EnsureRootFolder(Owner());
    ASSERT_TRUE(root.Ok()) << root.GetError().message;
    root_id_ = root.Value().id;
    root_name_ = root.Value().name;

    auto other_root = folders_service_->EnsureRootFolder(Other());
    ASSERT_TRUE(other_root.Ok());
    other_root_id_ = other_root.Value().id;
  }

  void TearDown() override {
    if (users_) {
      (void)users_->DeleteAllForTesting();
    }
    if (audit_) {
      (void)audit_->DeleteAllForTesting();
    }
    if (cleanup_repo_) {
      (void)cleanup_repo_->DeleteAllForTesting();
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

  Result<File> Upload(const Actor& actor, const std::string& folder_id, const std::string& name,
                      const std::string& payload) {
    auto session = files_service_->BeginUpload(actor, UploadRequest{folder_id, name, "text/plain"});
    if (!session) {
      return session.GetError();
    }
    const auto upload = session.Value();
    auto written = upload->Write(payload.data(), payload.size());
    if (!written.Ok()) {
      return written.GetError();
    }
    return upload->Finish();
  }

  // 「最近改动时间」直接用 SQL 摆布：本用例要验的是**排序与过滤**，
  // 而不是写入时间戳本身；而连续上传的几个文件 updated_at 可能落在同一微秒，
  // 那样排序就退化成按 id（UUID）比大小 —— 断言会变成随机成功/失败。
  void SetUpdatedSecondsAgo(const std::string& file_id, int seconds) {
    auto lease = pool_->Acquire();
    ASSERT_TRUE(lease != nullptr) << "数据库繁忙，拿不到连接";
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(file_id);
    params.append(seconds);
    tx.exec(
        "UPDATE files SET updated_at = now() - ($2::int * interval '1 second') "
        "WHERE id = $1::uuid",
        params);
    tx.commit();
  }

  std::vector<std::string> Names(const RecentListing& listing) {
    std::vector<std::string> names;
    names.reserve(listing.files.size());
    for (const auto& item : listing.files) {
      names.push_back(item.file.original_name);
    }
    return names;
  }

  std::vector<std::string> Ids(const RecentListing& listing) {
    std::vector<std::string> ids;
    ids.reserve(listing.files.size());
    for (const auto& item : listing.files) {
      ids.push_back(item.file.id);
    }
    return ids;
  }

  static Pagination Page(int page, int page_size) {
    Pagination pagination;
    pagination.page = page;
    pagination.page_size = page_size;
    return pagination;
  }

  testing::TempDir storage_dir_{"recent_storage"};
  std::shared_ptr<ConnectionPool> pool_;
  std::shared_ptr<UserRepository> users_;
  std::shared_ptr<FolderRepository> folders_;
  std::shared_ptr<FileRepository> files_;
  std::shared_ptr<TrashRepository> trash_repo_;
  std::shared_ptr<AuditLogRepository> audit_;
  std::shared_ptr<CleanupTaskRepository> cleanup_repo_;
  std::shared_ptr<PermissionRepository> permissions_repo_;
  std::shared_ptr<ShareLinkRepository> shares_repo_;
  std::shared_ptr<OwnerBasedPermissionChecker> permissions_;
  std::shared_ptr<LocalObjectStorage> storage_;
  std::shared_ptr<FolderService> folders_service_;
  std::shared_ptr<FileService> files_service_;
  std::shared_ptr<TrashService> trash_;
  User owner_;
  User other_;
  std::string root_id_;
  std::string root_name_;
  std::string other_root_id_;
};

TEST_F(RecentFilesTest, ListsOwnFilesNewestFirstAcrossFolders) {
  auto sub = folders_service_->Create(Owner(), root_id_, "项目资料");
  ASSERT_TRUE(sub.Ok()) << sub.GetError().message;

  const auto oldest = Upload(Owner(), root_id_, "旧文件.txt", "a");
  const auto middle = Upload(Owner(), sub.Value().id, "中间文件.txt", "b");
  const auto newest = Upload(Owner(), root_id_, "最新文件.txt", "c");
  const auto foreign = Upload(Other(), other_root_id_, "别人的文件.txt", "d");
  ASSERT_TRUE(oldest.Ok() && middle.Ok() && newest.Ok() && foreign.Ok());

  SetUpdatedSecondsAgo(oldest.Value().id, 300);
  SetUpdatedSecondsAgo(middle.Value().id, 200);
  SetUpdatedSecondsAgo(newest.Value().id, 100);

  const auto listing = files_service_->ListRecent(Owner(), Page(1, 50));
  ASSERT_TRUE(listing.Ok()) << listing.GetError().message;

  // 跨文件夹、按最后改动时间倒序
  EXPECT_EQ(Names(listing.Value()),
            (std::vector<std::string>{"最新文件.txt", "中间文件.txt", "旧文件.txt"}));
  // 总数只看自己的
  EXPECT_EQ(listing.Value().total, 3U);
  for (const auto& item : listing.Value().files) {
    EXPECT_NE(item.file.id, foreign.Value().id) << "别人的文件绝不能出现在我的「最近文件」里";
    EXPECT_EQ(item.file.owner_id, owner_.id);
  }
}

TEST_F(RecentFilesTest, TellsTheUserWhichFolderEachFileIsIn) {
  auto sub = folders_service_->Create(Owner(), root_id_, "合同");
  ASSERT_TRUE(sub.Ok()) << sub.GetError().message;

  const auto in_root = Upload(Owner(), root_id_, "根目录文件.txt", "a");
  const auto in_sub = Upload(Owner(), sub.Value().id, "子目录文件.txt", "b");
  ASSERT_TRUE(in_root.Ok() && in_sub.Ok());

  const auto listing = files_service_->ListRecent(Owner(), Page(1, 50));
  ASSERT_TRUE(listing.Ok()) << listing.GetError().message;
  ASSERT_EQ(listing.Value().files.size(), 2U);

  for (const auto& item : listing.Value().files) {
    if (item.file.original_name == "子目录文件.txt") {
      EXPECT_EQ(item.folder_id, sub.Value().id);
      EXPECT_EQ(item.folder_name, "合同");
    } else {
      EXPECT_EQ(item.folder_id, root_id_);
      EXPECT_EQ(item.folder_name, root_name_);
    }
  }
}

TEST_F(RecentFilesTest, HidesTrashedFilesAndShrinksTheTotal) {
  const auto keep = Upload(Owner(), root_id_, "留着.txt", "a");
  const auto gone = Upload(Owner(), root_id_, "删掉.txt", "b");
  ASSERT_TRUE(keep.Ok() && gone.Ok());
  SetUpdatedSecondsAgo(keep.Value().id, 100);
  SetUpdatedSecondsAgo(gone.Value().id, 10);  // 删掉的那个本来是最新的

  ASSERT_TRUE(trash_->SoftDeleteFile(Owner(), gone.Value().id).Ok());

  const auto listing = files_service_->ListRecent(Owner(), Page(1, 50));
  ASSERT_TRUE(listing.Ok()) << listing.GetError().message;
  EXPECT_EQ(Names(listing.Value()), (std::vector<std::string>{"留着.txt"}));
  EXPECT_EQ(listing.Value().total, 1U) << "total 必须跟过滤后的结果一致，不能还是 2";
}

TEST_F(RecentFilesTest, IsEmptyForANewAccount) {
  const auto listing = files_service_->ListRecent(Other(), Page(1, 50));
  ASSERT_TRUE(listing.Ok()) << listing.GetError().message;
  EXPECT_TRUE(listing.Value().files.empty());
  EXPECT_EQ(listing.Value().total, 0U);
  EXPECT_EQ(listing.Value().page, 1);
  EXPECT_EQ(listing.Value().page_size, 50);
}

TEST_F(RecentFilesTest, PaginatesWithoutGapsOrDuplicates) {
  std::vector<std::string> expected;  // 从新到旧
  for (int index = 0; index < 5; ++index) {
    const std::string name = "文件" + std::to_string(index) + ".txt";
    const auto uploaded = Upload(Owner(), root_id_, name, "payload-" + std::to_string(index));
    ASSERT_TRUE(uploaded.Ok()) << uploaded.GetError().message;
    // index 越大越新
    SetUpdatedSecondsAgo(uploaded.Value().id, 100 - index * 10);
    expected.push_back(name);
  }
  std::reverse(expected.begin(), expected.end());

  std::vector<std::string> collected;
  for (int page = 1; page <= 3; ++page) {
    const auto listing = files_service_->ListRecent(Owner(), Page(page, 2));
    ASSERT_TRUE(listing.Ok()) << listing.GetError().message;
    EXPECT_EQ(listing.Value().total, 5U) << "每页的 total 都是全量";
    EXPECT_EQ(listing.Value().page, page);
    EXPECT_EQ(listing.Value().page_size, 2);
    const auto names = Names(listing.Value());
    if (page < 3) {
      EXPECT_EQ(names.size(), 2U) << "第 " << page << " 页应满页";
    } else {
      EXPECT_EQ(names.size(), 1U) << "最后一页只剩余数，不能重复前面的";
    }
    collected.insert(collected.end(), names.begin(), names.end());
  }

  EXPECT_EQ(collected, expected) << "三页拼起来必须正好是全部 5 个、不重不漏且顺序正确";

  // 越界页：空列表，但 total 仍然是 5
  const auto beyond = files_service_->ListRecent(Owner(), Page(9, 2));
  ASSERT_TRUE(beyond.Ok());
  EXPECT_TRUE(beyond.Value().files.empty());
  EXPECT_EQ(beyond.Value().total, 5U);

  // page_size = 1（下界）也要能逐页取完
  const auto single = files_service_->ListRecent(Owner(), Page(1, 1));
  ASSERT_TRUE(single.Ok());
  ASSERT_EQ(single.Value().files.size(), 1U);
  EXPECT_EQ(single.Value().files.front().file.original_name, expected.front());
}

}  // namespace
}  // namespace lan_file_hub
