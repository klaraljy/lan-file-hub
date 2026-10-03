// 搜索（`GET /api/v1/search?q=`，2026-10-03）的集成测试。
//
// 为什么要有：搜索是**跨目录的大小写不敏感子串匹配**，而用户输入要变成 SQL LIKE 模式 ——
// 这里有三个容易出错的地方，全都必须钉住：
//   ① 只能搜到自己的（越权搜索比越权下载更隐蔽）；
//   ② 回收站里的不能出现（与「最近文件」同口径）；
//   ③ `%` / `_` 必须当普通字符：搜 `%` 不能变成"匹配全部"。
// 另外：文件夹也要能搜到、文件要分页、空关键字要明确报错。
//
// 需要真实 PostgreSQL。⛔ 必须用独立测试库（AGENTS.md §11）。
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

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

class SearchTest : public ::testing::Test {
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

    owner_ = MakeUser("searchowner");
    other_ = MakeUser("searchother");
    ASSERT_TRUE(users_->Create(owner_).Ok());
    ASSERT_TRUE(users_->Create(other_).Ok());

    auto root = folders_service_->EnsureRootFolder(Owner());
    ASSERT_TRUE(root.Ok()) << root.GetError().message;
    root_id_ = root.Value().id;

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

  std::vector<std::string> Names(const SearchListing& listing) {
    std::vector<std::string> names;
    names.reserve(listing.files.size());
    for (const auto& item : listing.files) {
      names.push_back(item.file.original_name);
    }
    return names;
  }

  std::vector<std::string> FolderNames(const SearchListing& listing) {
    std::vector<std::string> names;
    names.reserve(listing.folders.size());
    for (const auto& folder : listing.folders) {
      names.push_back(folder.name);
    }
    return names;
  }

  static Pagination Page(int page, int page_size) {
    Pagination pagination;
    pagination.page = page;
    pagination.page_size = page_size;
    return pagination;
  }

  testing::TempDir storage_dir_{"search_storage"};
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
  std::string other_root_id_;
};

TEST_F(SearchTest, MatchesSubstringAcrossFoldersAndIgnoresCase) {
  auto sub = folders_service_->Create(Owner(), root_id_, "2026 报表");
  ASSERT_TRUE(sub.Ok()) << sub.GetError().message;

  ASSERT_TRUE(Upload(Owner(), root_id_, "季度报表.xlsx", "a").Ok());
  ASSERT_TRUE(Upload(Owner(), sub.Value().id, "REPORT-final.pdf", "b").Ok());
  ASSERT_TRUE(Upload(Owner(), root_id_, "照片.jpg", "c").Ok());
  ASSERT_TRUE(Upload(Other(), other_root_id_, "别人的报表.xlsx", "d").Ok());

  // 中文子串 + 跨目录
  const auto chinese = files_service_->Search(Owner(), "报表", Page(1, 50));
  ASSERT_TRUE(chinese.Ok()) << chinese.GetError().message;
  EXPECT_EQ(Names(chinese.Value()), (std::vector<std::string>{"季度报表.xlsx"}));
  EXPECT_EQ(chinese.Value().total, 1U) << "别人的文件绝不能算进来";
  EXPECT_EQ(chinese.Value().query, "报表");

  // 大小写不敏感（用户敲小写也要能搜到大写命名的文件）
  const auto lower = files_service_->Search(Owner(), "report", Page(1, 50));
  ASSERT_TRUE(lower.Ok()) << lower.GetError().message;
  EXPECT_EQ(Names(lower.Value()), (std::vector<std::string>{"REPORT-final.pdf"}));

  // 文件夹也能搜到（用户搜一个词，文件和目录都应出现）
  const auto folders = files_service_->Search(Owner(), "报表", Page(1, 50));
  ASSERT_TRUE(folders.Ok());
  EXPECT_EQ(FolderNames(folders.Value()), (std::vector<std::string>{"2026 报表"}));
}

TEST_F(SearchTest, TellsWhichFolderEachMatchIsIn) {
  auto sub = folders_service_->Create(Owner(), root_id_, "项目A");
  ASSERT_TRUE(sub.Ok());
  ASSERT_TRUE(Upload(Owner(), sub.Value().id, "需求说明.docx", "x").Ok());

  const auto listing = files_service_->Search(Owner(), "需求", Page(1, 50));
  ASSERT_TRUE(listing.Ok()) << listing.GetError().message;
  ASSERT_EQ(listing.Value().files.size(), 1U);
  EXPECT_EQ(listing.Value().files.front().folder_id, sub.Value().id);
  EXPECT_EQ(listing.Value().files.front().folder_name, "项目A");
}

TEST_F(SearchTest, HidesTrashedMatchesAndShrinksTotal) {
  const auto keep = Upload(Owner(), root_id_, "保留-报表.txt", "a");
  const auto gone = Upload(Owner(), root_id_, "删除-报表.txt", "b");
  ASSERT_TRUE(keep.Ok() && gone.Ok());

  ASSERT_TRUE(trash_->SoftDeleteFile(Owner(), gone.Value().id).Ok());

  const auto listing = files_service_->Search(Owner(), "报表", Page(1, 50));
  ASSERT_TRUE(listing.Ok()) << listing.GetError().message;
  EXPECT_EQ(Names(listing.Value()), (std::vector<std::string>{"保留-报表.txt"}));
  EXPECT_EQ(listing.Value().total, 1U) << "total 必须跟过滤后的结果一致";
}

// ⛔ 这条是本文件里最重要的：不转义通配符的话，用户搜一个 % 会"搜出全部文件"。
TEST_F(SearchTest, WildcardsAreLiteralCharacters) {
  ASSERT_TRUE(Upload(Owner(), root_id_, "折扣100%如假包换.txt", "a").Ok());
  ASSERT_TRUE(Upload(Owner(), root_id_, "普通文件.txt", "b").Ok());
  ASSERT_TRUE(Upload(Owner(), root_id_, "a_b.txt", "c").Ok());
  ASSERT_TRUE(Upload(Owner(), root_id_, "axb.txt", "d").Ok());

  const auto percent = files_service_->Search(Owner(), "%", Page(1, 50));
  ASSERT_TRUE(percent.Ok()) << percent.GetError().message;
  EXPECT_EQ(Names(percent.Value()), (std::vector<std::string>{"折扣100%如假包换.txt"}))
      << "搜 % 只能命中名字里真的有 % 的文件";

  const auto underscore = files_service_->Search(Owner(), "a_b", Page(1, 50));
  ASSERT_TRUE(underscore.Ok()) << underscore.GetError().message;
  EXPECT_EQ(Names(underscore.Value()), (std::vector<std::string>{"a_b.txt"}))
      << "下划线是普通字符，不该匹配 axb.txt";
}

TEST_F(SearchTest, RejectsEmptyAndOverlongQueries) {
  const auto empty = files_service_->Search(Owner(), "", Page(1, 50));
  ASSERT_FALSE(empty.Ok());
  EXPECT_EQ(empty.GetError().code, "INVALID_ARGUMENT");

  // 只有空白也算空
  const auto blank = files_service_->Search(Owner(), "   ", Page(1, 50));
  ASSERT_FALSE(blank.Ok());
  EXPECT_EQ(blank.GetError().code, "INVALID_ARGUMENT");

  const auto too_long = files_service_->Search(Owner(), std::string(200, 'x'), Page(1, 50));
  ASSERT_FALSE(too_long.Ok());
  EXPECT_EQ(too_long.GetError().code, "INVALID_ARGUMENT");

  // 关键字两端的空白要去掉后再匹配（粘进来常常带空格）
  ASSERT_TRUE(Upload(Owner(), root_id_, "空格测试.txt", "a").Ok());
  const auto padded = files_service_->Search(Owner(), "  空格测试  ", Page(1, 50));
  ASSERT_TRUE(padded.Ok()) << padded.GetError().message;
  EXPECT_EQ(Names(padded.Value()), (std::vector<std::string>{"空格测试.txt"}));
}

TEST_F(SearchTest, PaginatesMatchesWithoutGapsOrDuplicates) {
  std::vector<std::string> expected;
  for (int index = 0; index < 5; ++index) {
    const std::string name = "批量-搜索-" + std::to_string(index) + ".txt";
    ASSERT_TRUE(Upload(Owner(), root_id_, name, "payload-" + std::to_string(index)).Ok());
    expected.push_back(name);
  }
  std::sort(expected.begin(), expected.end());

  std::vector<std::string> collected;
  for (int page = 1; page <= 3; ++page) {
    const auto listing = files_service_->Search(Owner(), "批量-搜索", Page(page, 2));
    ASSERT_TRUE(listing.Ok()) << listing.GetError().message;
    EXPECT_EQ(listing.Value().total, 5U) << "每页的 total 都是全量";
    const auto names = Names(listing.Value());
    EXPECT_EQ(names.size(), page < 3 ? 2U : 1U);
    collected.insert(collected.end(), names.begin(), names.end());
  }
  std::sort(collected.begin(), collected.end());
  EXPECT_EQ(collected, expected) << "三页拼起来必须正好是全部 5 个、不重不漏";
}

}  // namespace
}  // namespace lan_file_hub
