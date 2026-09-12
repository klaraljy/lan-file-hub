// 阶段七集成测试：文件版本与乐观并发控制。
// 需要真实 PostgreSQL（版本号、If-Match 与配额都依赖事务与行锁）。
//
// 覆盖：上传新版本、版本列表、历史版本下载、恢复历史版本、重复恢复幂等、
//       两人同时修改同一文件、旧版本不被覆盖、加版本时的配额检查。
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "fixtures/temp_dir.h"
#include "lan_file_hub/application/file_service.h"
#include "lan_file_hub/application/folder_service.h"
#include "lan_file_hub/application/permission_checker.h"
#include "lan_file_hub/application/upload_service.h"
#include "lan_file_hub/common/config.h"
#include "lan_file_hub/common/sha256.h"
#include "lan_file_hub/common/uuid.h"
#include "lan_file_hub/infrastructure/database.h"
#include "lan_file_hub/infrastructure/file_repository.h"
#include "lan_file_hub/infrastructure/folder_repository.h"
#include "lan_file_hub/infrastructure/upload_session_repository.h"
#include "lan_file_hub/infrastructure/user_repository.h"
#include "lan_file_hub/storage/local_object_storage.h"

namespace lan_file_hub {
namespace {

constexpr std::uint32_t kMiB = 1024U * 1024U;
constexpr std::uint32_t kChunk = 1U * kMiB;

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

std::string Repeat(char c, std::size_t count) {
  return std::string(count, c);
}

class VersionTest : public ::testing::Test {
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
    sessions_ = std::make_shared<UploadSessionRepository>(*pool_);
    permissions_ = std::make_shared<OwnerBasedPermissionChecker>();

    if (!users_->DeleteAllForTesting().Ok()) {
      Skip("users 表不可用，请先运行 lan_file_hub_migrate");
      return;
    }
    if (!sessions_->DeleteAllForTesting().Ok()) {
      Skip("upload_sessions 表不可用，请先运行 lan_file_hub_migrate");
      return;
    }
    (void)files_->DeleteAllForTesting();

    auto storage = LocalObjectStorage::Create(storage_dir_.Path());
    ASSERT_TRUE(storage.Ok()) << storage.GetError().message;
    storage_ = storage.Value();

    folders_service_ = std::make_shared<FolderService>(*folders_, *permissions_);
    files_service_ = std::make_shared<FileService>(*files_, *folders_, *storage_, *permissions_,
                                                   FileServiceOptions{64ULL * kMiB});

    owner_ = MakeUser("verowner");
    other_ = MakeUser("verother");
    ASSERT_TRUE(users_->Create(owner_).Ok());
    ASSERT_TRUE(users_->Create(other_).Ok());

    auto root = folders_service_->EnsureRootFolder(Owner());
    ASSERT_TRUE(root.Ok()) << root.GetError().message;
    root_id_ = root.Value().id;

    uploads_ = std::make_shared<UploadService>(*sessions_, *files_, *folders_, *users_, *storage_,
                                               *permissions_, Options());
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

  UploadServiceOptions Options() {
    UploadServiceOptions options;
    options.default_chunk_size = kChunk;
    options.max_concurrent_uploads = 32;
    options.max_concurrent_uploads_per_user = 16;
    return options;
  }

  User MakeUser(const std::string& name) {
    User user;
    user.id = GenerateUuidV4();
    user.username = name;
    user.email = name + "@example.com";
    user.password_hash = "$argon2id$v=19$m=1024,t=1,p=1$c2FsdA$aGFzaA";
    user.role = UserRole::kUser;
    user.status = UserStatus::kActive;
    user.storage_quota_bytes = 64ULL * kMiB;
    user.used_storage_bytes = 0;
    return user;
  }

  Actor Owner() const { return Actor{owner_.id, owner_.role}; }
  Actor Other() const { return Actor{other_.id, other_.role}; }

  // 上传一个新文件（第一个版本）
  Result<File> CreateFile(const std::string& name, const std::string& payload) {
    return UploadContent(root_id_, name, payload, std::nullopt, std::nullopt);
  }

  // 给已有文件加新版本
  Result<File> AddNewVersion(const std::string& file_id, const std::string& payload,
                             std::optional<std::string> if_match) {
    return UploadContent(root_id_, "", payload, file_id, if_match);
  }

  Result<File> UploadContent(const std::string& folder_id, const std::string& name,
                             const std::string& payload, std::optional<std::string> target_file_id,
                             std::optional<std::string> expected_version_id) {
    CreateUploadRequest request;
    request.folder_id = folder_id;
    request.file_name = name;
    request.total_size = payload.size();
    request.chunk_size = kChunk;
    request.sha256 = Sha256Hex(payload);
    request.target_file_id = target_file_id;
    request.expected_version_id = expected_version_id;

    auto created = uploads_->CreateSession(Owner(), request);
    if (!created) {
      return created.GetError();
    }
    const std::string upload_id = created.Value().session.id;

    const std::uint32_t total_chunks = created.Value().session.total_chunks;
    for (std::uint32_t index = 0; index < total_chunks; ++index) {
      const std::size_t offset = static_cast<std::size_t>(index) * kChunk;
      const std::size_t length =
          offset >= payload.size() ? 0 : std::min<std::size_t>(kChunk, payload.size() - offset);
      const std::string part = payload.substr(offset, length);
      auto start = uploads_->BeginChunk(Owner(), upload_id, index, Sha256Hex(part));
      if (!start) {
        return start.GetError();
      }
      if (!start.Value().already_present) {
        auto written = start.Value().session->Write(part.data(), part.size());
        if (!written.Ok()) {
          return written.GetError();
        }
        auto finished = start.Value().session->Finish();
        if (!finished.Ok()) {
          return finished.GetError();
        }
      }
    }
    return uploads_->Complete(Owner(), upload_id);
  }

  std::uint64_t UsedBytes(const std::string& user_id) {
    auto user = users_->FindById(user_id);
    return user.Ok() && user.Value().has_value() ? user.Value()->used_storage_bytes : 0;
  }

  testing::TempDir storage_dir_{"version_storage"};
  std::shared_ptr<ConnectionPool> pool_;
  std::shared_ptr<UserRepository> users_;
  std::shared_ptr<FolderRepository> folders_;
  std::shared_ptr<FileRepository> files_;
  std::shared_ptr<UploadSessionRepository> sessions_;
  std::shared_ptr<OwnerBasedPermissionChecker> permissions_;
  std::shared_ptr<LocalObjectStorage> storage_;
  std::shared_ptr<FolderService> folders_service_;
  std::shared_ptr<FileService> files_service_;
  std::shared_ptr<UploadService> uploads_;
  User owner_;
  User other_;
  std::string root_id_;
};

// ---- 新版本 -----------------------------------------------------------------

TEST_F(VersionTest, UploadingNewContentCreatesNewVersion) {
  const std::string v1 = Repeat('a', 2 * kMiB);
  auto created = CreateFile("doc.bin", v1);
  ASSERT_TRUE(created.Ok()) << created.GetError().message;
  const std::string v1_version_id = created.Value().current_version_id.value_or("");
  ASSERT_FALSE(v1_version_id.empty());
  const std::string v1_key = created.Value().storage_key;

  const std::string v2 = Repeat('b', 3 * kMiB);
  auto updated = AddNewVersion(created.Value().id, v2, v1_version_id);
  ASSERT_TRUE(updated.Ok()) << updated.GetError().message;

  EXPECT_EQ(updated.Value().id, created.Value().id) << "版本更新不能换文件 id";
  EXPECT_EQ(updated.Value().original_name, "doc.bin") << "文件名不变";
  EXPECT_EQ(updated.Value().size_bytes, v2.size());
  EXPECT_EQ(updated.Value().content_hash, Sha256Hex(v2));
  EXPECT_NE(updated.Value().current_version_id.value_or(""), v1_version_id);
  EXPECT_NE(updated.Value().storage_key, v1_key) << "新内容必须用新的对象 Key";

  // 版本列表：两条，新的在前
  auto versions = files_service_->ListVersions(Owner(), created.Value().id);
  ASSERT_TRUE(versions.Ok()) << versions.GetError().message;
  ASSERT_EQ(versions.Value().size(), 2U);
  EXPECT_EQ(versions.Value()[0].version_number, 2);
  EXPECT_TRUE(versions.Value()[0].is_current);
  EXPECT_EQ(versions.Value()[1].version_number, 1);
  EXPECT_FALSE(versions.Value()[1].is_current);

  // 旧版本对象必须还在（不允许覆盖当前版本对象）
  EXPECT_TRUE(storage_->Exists(v1_key));
  EXPECT_EQ(versions.Value()[1].content_hash, Sha256Hex(v1));
  EXPECT_EQ(versions.Value()[1].storage_key, v1_key);
}

TEST_F(VersionTest, CurrentVersionQueryReturnsLatest) {
  auto created = CreateFile("cur.bin", Repeat('c', kMiB));
  ASSERT_TRUE(created.Ok());
  const std::string v1 = created.Value().current_version_id.value_or("");

  auto current = files_->FindCurrentVersion(created.Value().id);
  ASSERT_TRUE(current.Ok());
  ASSERT_TRUE(current.Value().has_value());
  EXPECT_EQ(current.Value()->id, v1);
  EXPECT_TRUE(current.Value()->is_current);

  auto updated = AddNewVersion(created.Value().id, Repeat('d', kMiB), v1);
  ASSERT_TRUE(updated.Ok()) << updated.GetError().message;

  auto after = files_->FindCurrentVersion(created.Value().id);
  ASSERT_TRUE(after.Ok());
  ASSERT_TRUE(after.Value().has_value());
  EXPECT_EQ(after.Value()->id, updated.Value().current_version_id.value_or(""));
  EXPECT_NE(after.Value()->id, v1) << "当前版本必须指向最新版本";
}

TEST_F(VersionTest, HistoricalVersionCanBeDownloaded) {
  const std::string v1 = Repeat('e', 2 * kMiB);
  auto created = CreateFile("hist.bin", v1);
  ASSERT_TRUE(created.Ok());
  const std::string v1_id = created.Value().current_version_id.value_or("");

  auto updated = AddNewVersion(created.Value().id, Repeat('f', kMiB), v1_id);
  ASSERT_TRUE(updated.Ok()) << updated.GetError().message;

  // 当前内容是新版本
  auto now = files_service_->PrepareDownload(Owner(), created.Value().id);
  ASSERT_TRUE(now.Ok());
  EXPECT_EQ(now.Value().file.content_hash, Sha256Hex(Repeat('f', kMiB)));

  // 历史版本仍然是旧内容，而且物理路径对应旧对象
  auto old = files_service_->PrepareVersionDownload(Owner(), created.Value().id, v1_id);
  ASSERT_TRUE(old.Ok()) << old.GetError().message;
  EXPECT_EQ(old.Value().version.version_number, 1);
  EXPECT_EQ(old.Value().version.content_hash, Sha256Hex(v1));
  EXPECT_EQ(old.Value().version.size_bytes, v1.size());

  std::ifstream input(old.Value().absolute_path, std::ios::binary);
  const std::string content((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
  EXPECT_EQ(content, v1) << "下载到的必须是该版本的真实内容";
}

// ---- 恢复 -------------------------------------------------------------------

TEST_F(VersionTest, RestoreCreatesNewVersionInsteadOfOverwriting) {
  const std::string v1 = Repeat('g', 2 * kMiB);
  const std::string v2 = Repeat('h', 3 * kMiB);
  auto created = CreateFile("restore.bin", v1);
  ASSERT_TRUE(created.Ok());
  const std::string v1_id = created.Value().current_version_id.value_or("");
  auto updated = AddNewVersion(created.Value().id, v2, v1_id);
  ASSERT_TRUE(updated.Ok()) << updated.GetError().message;

  auto restored = files_service_->RestoreVersion(Owner(), created.Value().id, v1_id, std::nullopt);
  ASSERT_TRUE(restored.Ok()) << restored.GetError().message;
  EXPECT_EQ(restored.Value().content_hash, Sha256Hex(v1)) << "当前内容回到 v1";
  EXPECT_EQ(restored.Value().size_bytes, v1.size());

  // 恢复是「追加一个内容等于 v1 的新版本」，不是删掉 v2
  auto versions = files_service_->ListVersions(Owner(), created.Value().id);
  ASSERT_TRUE(versions.Ok());
  ASSERT_EQ(versions.Value().size(), 3U) << "恢复不能删掉原来的 v2";
  EXPECT_EQ(versions.Value()[0].version_number, 3);
  EXPECT_TRUE(versions.Value()[0].is_current);
  EXPECT_EQ(versions.Value()[1].version_number, 2);
  EXPECT_EQ(versions.Value()[1].content_hash, Sha256Hex(v2));
  EXPECT_FALSE(versions.Value()[1].is_current);
}

TEST_F(VersionTest, RepeatedRestoreIsIdempotent) {
  const std::string v1 = Repeat('i', kMiB);
  auto created = CreateFile("again.bin", v1);
  ASSERT_TRUE(created.Ok());
  const std::string v1_id = created.Value().current_version_id.value_or("");
  auto updated = AddNewVersion(created.Value().id, Repeat('j', kMiB), v1_id);
  ASSERT_TRUE(updated.Ok()) << updated.GetError().message;
  const std::uint64_t used_after_v2 = UsedBytes(owner_.id);

  auto first = files_service_->RestoreVersion(Owner(), created.Value().id, v1_id, std::nullopt);
  ASSERT_TRUE(first.Ok()) << first.GetError().message;
  const std::string version_after_first_restore = first.Value().current_version_id.value_or("");

  // 再恢复一次：当前内容已经等于 v1，不能再追加一个一模一样的版本
  auto second = files_service_->RestoreVersion(Owner(), created.Value().id, v1_id, std::nullopt);
  ASSERT_TRUE(second.Ok()) << second.GetError().message;
  EXPECT_EQ(second.Value().current_version_id.value_or(""), version_after_first_restore)
      << "重复恢复不能产生新版本";

  auto versions = files_service_->ListVersions(Owner(), created.Value().id);
  ASSERT_TRUE(versions.Ok());
  EXPECT_EQ(versions.Value().size(), 3U) << "v1 + v2 + 恢复版本，重复恢复不再增加";
  // 重复恢复也不该再占配额
  EXPECT_EQ(UsedBytes(owner_.id), used_after_v2 + v1.size());
}

TEST_F(VersionTest, RestoreRequiresWritePermissionAndExistingVersion) {
  auto created = CreateFile("perm.bin", Repeat('k', kMiB));
  ASSERT_TRUE(created.Ok());
  const std::string v1_id = created.Value().current_version_id.value_or("");

  // 他人：一律当作不存在
  auto denied = files_service_->RestoreVersion(Other(), created.Value().id, v1_id, std::nullopt);
  ASSERT_FALSE(denied.Ok());
  EXPECT_EQ(denied.GetError().code, "RESOURCE_NOT_FOUND");

  // 不存在的版本
  auto missing =
      files_service_->RestoreVersion(Owner(), created.Value().id, GenerateUuidV4(), std::nullopt);
  ASSERT_FALSE(missing.Ok());
  EXPECT_EQ(missing.GetError().code, "RESOURCE_NOT_FOUND");
}

// ---- 乐观并发 ---------------------------------------------------------------

TEST_F(VersionTest, StaleIfMatchIsRejectedWithCurrentVersion) {
  auto created = CreateFile("race.bin", Repeat('l', kMiB));
  ASSERT_TRUE(created.Ok());
  const std::string v1_id = created.Value().current_version_id.value_or("");

  // 甲先提交，当前版本变成 v2
  auto first = AddNewVersion(created.Value().id, Repeat('m', kMiB), v1_id);
  ASSERT_TRUE(first.Ok()) << first.GetError().message;
  const std::string v2_id = first.Value().current_version_id.value_or("");

  // 乙拿着过期的 v1 提交：必须 409，并告诉他当前版本是哪个
  auto stale = AddNewVersion(created.Value().id, Repeat('n', kMiB), v1_id);
  ASSERT_FALSE(stale.Ok());
  EXPECT_EQ(stale.GetError().code, "FILE_VERSION_CONFLICT");
  ASSERT_TRUE(stale.GetError().details.count("current_version_id") > 0);
  EXPECT_EQ(stale.GetError().details.at("current_version_id"), v2_id);

  // 冲突不能留下半成品：版本数还是 2，内容还是 v2
  auto versions = files_service_->ListVersions(Owner(), created.Value().id);
  ASSERT_TRUE(versions.Ok());
  EXPECT_EQ(versions.Value().size(), 2U);
  auto metadata = files_service_->GetMetadata(Owner(), created.Value().id);
  ASSERT_TRUE(metadata.Ok());
  EXPECT_EQ(metadata.Value().content_hash, Sha256Hex(Repeat('m', kMiB)));
}

TEST_F(VersionTest, RetryWithFreshIfMatchSucceeds) {
  auto created = CreateFile("retry.bin", Repeat('o', kMiB));
  ASSERT_TRUE(created.Ok());
  const std::string v1_id = created.Value().current_version_id.value_or("");
  auto first = AddNewVersion(created.Value().id, Repeat('p', kMiB), v1_id);
  ASSERT_TRUE(first.Ok()) << first.GetError().message;

  // 刷新拿到最新版本后重试即可成功
  auto metadata = files_service_->GetMetadata(Owner(), created.Value().id);
  ASSERT_TRUE(metadata.Ok());
  const std::string latest = metadata.Value().current_version_id.value_or("");
  auto retried = AddNewVersion(created.Value().id, Repeat('q', kMiB), latest);
  ASSERT_TRUE(retried.Ok()) << retried.GetError().message;
  EXPECT_EQ(retried.Value().content_hash, Sha256Hex(Repeat('q', kMiB)));
}

TEST_F(VersionTest, TwoConcurrentUploadsOfNewVersionOnlyOneWins) {
  auto created = CreateFile("concurrent.bin", Repeat('r', kMiB));
  ASSERT_TRUE(created.Ok());
  const std::string v1_id = created.Value().current_version_id.value_or("");
  const std::string file_id = created.Value().id;

  // 两个线程同时带着同一个 If-Match 提交不同内容
  const std::string payload_a = Repeat('s', 2 * kMiB);
  const std::string payload_b = Repeat('t', 2 * kMiB);

  std::atomic<int> succeeded{0};
  std::atomic<int> conflicted{0};
  std::vector<std::string> hashes(2);
  std::vector<std::thread> workers;
  for (int i = 0; i < 2; ++i) {
    workers.emplace_back([&, i]() {
      const std::string& payload = i == 0 ? payload_a : payload_b;
      auto result = AddNewVersion(file_id, payload, v1_id);
      if (result.Ok()) {
        hashes[static_cast<std::size_t>(i)] = result.Value().content_hash;
        succeeded.fetch_add(1);
      } else if (result.GetError().code == "FILE_VERSION_CONFLICT" ||
                 result.GetError().code == "RESOURCE_CONFLICT") {
        conflicted.fetch_add(1);
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }

  EXPECT_EQ(succeeded.load(), 1) << "同一个 If-Match 只能有一个成功";
  EXPECT_EQ(conflicted.load(), 1) << "另一个必须是可解释的版本冲突";

  // 版本号不能重复，也不能丢版本
  auto versions = files_service_->ListVersions(Owner(), file_id);
  ASSERT_TRUE(versions.Ok());
  ASSERT_EQ(versions.Value().size(), 2U);
  EXPECT_EQ(versions.Value()[0].version_number, 2);
  EXPECT_EQ(versions.Value()[1].version_number, 1);
  EXPECT_EQ(versions.Value()[0].content_hash, hashes[0].empty() ? hashes[1] : hashes[0]);
}

TEST_F(VersionTest, UploadWithoutIfMatchAppendsWithoutOverwriting) {
  auto created = CreateFile("noifmatch.bin", Repeat('u', kMiB));
  ASSERT_TRUE(created.Ok());
  const std::string v1_id = created.Value().current_version_id.value_or("");

  // 不声明 If-Match：按追加处理，绝不覆盖已有版本
  auto appended = AddNewVersion(created.Value().id, Repeat('v', kMiB), std::nullopt);
  ASSERT_TRUE(appended.Ok()) << appended.GetError().message;

  auto versions = files_service_->ListVersions(Owner(), created.Value().id);
  ASSERT_TRUE(versions.Ok());
  EXPECT_EQ(versions.Value().size(), 2U);
  EXPECT_EQ(versions.Value()[1].id, v1_id) << "旧版本必须还在";
}

TEST_F(VersionTest, VersionQuotaIsEnforcedAndCountsEveryVersion) {
  auto created = CreateFile("quota.bin", Repeat('w', 4 * kMiB));
  ASSERT_TRUE(created.Ok());
  const std::uint64_t after_v1 = UsedBytes(owner_.id);
  EXPECT_EQ(after_v1, 4 * kMiB);

  // 历史版本也占配额
  auto second = AddNewVersion(created.Value().id, Repeat('x', 4 * kMiB), std::nullopt);
  ASSERT_TRUE(second.Ok()) << second.GetError().message;
  EXPECT_EQ(UsedBytes(owner_.id), 8 * kMiB);

  // 配额不够时加版本必须被拒绝
  ASSERT_TRUE(users_->SetQuotaForTesting(owner_.id, after_v1 + kMiB).Ok());
  auto rejected = AddNewVersion(created.Value().id, Repeat('y', 3 * kMiB), std::nullopt);
  ASSERT_FALSE(rejected.Ok());
  EXPECT_EQ(rejected.GetError().code, "QUOTA_EXCEEDED");
  EXPECT_EQ(UsedBytes(owner_.id), 8 * kMiB) << "被拒绝的版本不能改变用量";
}

TEST_F(VersionTest, SameContentVersionIsNotDuplicated) {
  const std::string payload = Repeat('z', 2 * kMiB);
  auto created = CreateFile("same.bin", payload);
  ASSERT_TRUE(created.Ok());
  const std::string v1_id = created.Value().current_version_id.value_or("");

  // 再传一次完全相同的内容：不追加重复版本，也不重复计费
  auto again = AddNewVersion(created.Value().id, payload, std::nullopt);
  ASSERT_TRUE(again.Ok()) << again.GetError().message;
  EXPECT_EQ(again.Value().current_version_id.value_or(""), v1_id);

  auto versions = files_service_->ListVersions(Owner(), created.Value().id);
  ASSERT_TRUE(versions.Ok());
  EXPECT_EQ(versions.Value().size(), 1U);
  EXPECT_EQ(UsedBytes(owner_.id), payload.size());
}

}  // namespace
}  // namespace lan_file_hub
