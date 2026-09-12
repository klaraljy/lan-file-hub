// 阶段八集成测试：回收站、恢复、永久删除与异步清理。
// 需要真实 PostgreSQL（软删除状态、级联删除、清理任务都在数据库里）。
//
// 覆盖：文件/文件夹软删除与列表过滤、恢复（幂等、父目录不存在、同名冲突）、
//       永久删除（记录删除 + 对象入队 + 配额回收）、清理任务重试与退避、
//       引用计数保护、审计日志、过期回收站清理。
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "fixtures/temp_dir.h"
#include "lan_file_hub/application/cleanup_service.h"
#include "lan_file_hub/application/file_service.h"
#include "lan_file_hub/application/folder_service.h"
#include "lan_file_hub/application/permission_checker.h"
#include "lan_file_hub/application/trash_service.h"
#include "lan_file_hub/application/upload_service.h"
#include "lan_file_hub/common/config.h"
#include "lan_file_hub/common/pagination.h"
#include "lan_file_hub/common/sha256.h"
#include "lan_file_hub/common/uuid.h"
#include "lan_file_hub/infrastructure/audit_repository.h"
#include "lan_file_hub/infrastructure/database.h"
#include "lan_file_hub/infrastructure/file_repository.h"
#include "lan_file_hub/infrastructure/folder_repository.h"
#include "lan_file_hub/infrastructure/permission_repository.h"
#include "lan_file_hub/infrastructure/trash_repository.h"
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

class TrashTest : public ::testing::Test {
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
    if (!sessions_->DeleteAllForTesting().Ok()) {
      Skip("upload_sessions 表不可用，请先运行 lan_file_hub_migrate");
      return;
    }
    (void)files_->DeleteAllForTesting();
    if (!audit_->DeleteAllForTesting().Ok()) {
      Skip("audit_logs 表不可用，请先运行 lan_file_hub_migrate");
      return;
    }
    if (!cleanup_repo_->DeleteAllForTesting().Ok()) {
      Skip("cleanup_tasks 表不可用，请先运行 lan_file_hub_migrate");
      return;
    }

    auto storage = LocalObjectStorage::Create(storage_dir_.Path());
    ASSERT_TRUE(storage.Ok()) << storage.GetError().message;
    storage_ = storage.Value();

    folders_service_ = std::make_shared<FolderService>(*folders_, *permissions_);
    files_service_ = std::make_shared<FileService>(*files_, *folders_, *storage_, *permissions_,
                                                   FileServiceOptions{64ULL * kMiB});
    uploads_ = std::make_shared<UploadService>(*sessions_, *files_, *folders_, *users_, *storage_,
                                               *permissions_, Options());
    trash_ =
        std::make_shared<TrashService>(*trash_repo_, *files_, *folders_, *audit_, *cleanup_repo_,
                                       *permissions_repo_, *shares_repo_, *permissions_);
    cleanup_ =
        std::make_shared<CleanupService>(*cleanup_repo_, *files_, *storage_, *trash_, *sessions_);

    owner_ = MakeUser("trashowner");
    other_ = MakeUser("trashother");
    ASSERT_TRUE(users_->Create(owner_).Ok());
    ASSERT_TRUE(users_->Create(other_).Ok());

    auto root = folders_service_->EnsureRootFolder(Owner());
    ASSERT_TRUE(root.Ok()) << root.GetError().message;
    root_id_ = root.Value().id;
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

  Result<File> Upload(const std::string& folder_id, const std::string& name,
                      const std::string& payload) {
    CreateUploadRequest request;
    request.folder_id = folder_id;
    request.file_name = name;
    request.total_size = payload.size();
    request.chunk_size = kChunk;
    request.sha256 = Sha256Hex(payload);

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

  std::uint64_t TrashCount() {
    auto listing = trash_->List(Owner(), Pagination{}, "");
    return listing.Ok() ? listing.Value().total : 0;
  }

  testing::TempDir storage_dir_{"trash_storage"};
  std::shared_ptr<ConnectionPool> pool_;
  std::shared_ptr<UserRepository> users_;
  std::shared_ptr<FolderRepository> folders_;
  std::shared_ptr<FileRepository> files_;
  std::shared_ptr<UploadSessionRepository> sessions_;
  std::shared_ptr<TrashRepository> trash_repo_;
  std::shared_ptr<AuditLogRepository> audit_;
  std::shared_ptr<CleanupTaskRepository> cleanup_repo_;
  std::shared_ptr<PermissionRepository> permissions_repo_;
  std::shared_ptr<ShareLinkRepository> shares_repo_;
  std::shared_ptr<OwnerBasedPermissionChecker> permissions_;
  std::shared_ptr<LocalObjectStorage> storage_;
  std::shared_ptr<FolderService> folders_service_;
  std::shared_ptr<FileService> files_service_;
  std::shared_ptr<UploadService> uploads_;
  std::shared_ptr<TrashService> trash_;
  std::shared_ptr<CleanupService> cleanup_;
  User owner_;
  User other_;
  std::string root_id_;
};

// ---- 软删除与列表过滤 --------------------------------------------------------

TEST_F(TrashTest, SoftDeletedFileDisappearsFromListingAndAppearsInTrash) {
  const std::string payload = Repeat('a', 2 * kMiB);
  auto uploaded = Upload(root_id_, "gone.bin", payload);
  ASSERT_TRUE(uploaded.Ok()) << uploaded.GetError().message;
  ASSERT_TRUE(trash_->SoftDeleteFile(Owner(), uploaded.Value().id).Ok());

  // 普通列表必须过滤掉：FindById 仍然返回记录，但状态是 TRASHED
  auto metadata = files_service_->GetMetadata(Owner(), uploaded.Value().id);
  EXPECT_FALSE(metadata.Ok()) << "已删除的文件不能再通过普通元数据接口读到";
  auto in_trash = files_service_->GetMetadataIncludingTrashed(Owner(), uploaded.Value().id);
  ASSERT_TRUE(in_trash.Ok());
  EXPECT_EQ(in_trash.Value().status, FileStatus::kTrashed);
  EXPECT_TRUE(in_trash.Value().deleted_at.has_value());

  auto listing = files_->ListByFolder(root_id_, Pagination{}, "lower(f.original_name) ASC");
  ASSERT_TRUE(listing.Ok());
  EXPECT_TRUE(listing.Value().empty()) << "回收站里的文件不能出现在目录列表里";

  // 回收站里能看到，且带删除时间与大小
  auto trash_list = trash_->List(Owner(), Pagination{}, "");
  ASSERT_TRUE(trash_list.Ok()) << trash_list.GetError().message << " | "
                               << trash_list.GetError().detail;
  ASSERT_EQ(trash_list.Value().items.size(), 1U);
  EXPECT_EQ(trash_list.Value().items[0].name, "gone.bin");
  EXPECT_EQ(trash_list.Value().items[0].kind, "file");
  EXPECT_EQ(trash_list.Value().items[0].size_bytes, payload.size());
  EXPECT_FALSE(trash_list.Value().items[0].deleted_at.empty());

  // 磁盘对象不动：软删除之后对象还在（恢复是廉价的）
  EXPECT_TRUE(storage_->Exists(uploaded.Value().storage_key));
}

TEST_F(TrashTest, ObjectStillOccupiesQuotaWhileInTrash) {
  const std::string payload = Repeat('b', 3 * kMiB);
  auto uploaded = Upload(root_id_, "quota.bin", payload);
  ASSERT_TRUE(uploaded.Ok()) << uploaded.GetError().message;
  const std::uint64_t after_upload = UsedBytes(owner_.id);

  ASSERT_TRUE(trash_->SoftDeleteFile(Owner(), uploaded.Value().id).Ok());
  EXPECT_EQ(UsedBytes(owner_.id), after_upload) << "回收站里的文件仍占配额";
}

TEST_F(TrashTest, FolderDeletionMovesSubtreeFilesToTrash) {
  auto folder = folders_service_->Create(Owner(), root_id_, "docs");
  ASSERT_TRUE(folder.Ok()) << folder.GetError().message;
  auto child = folders_service_->Create(Owner(), folder.Value().id, "inner");
  ASSERT_TRUE(child.Ok());

  auto in_child = Upload(child.Value().id, "deep.bin", Repeat('c', kMiB));
  ASSERT_TRUE(in_child.Ok()) << in_child.GetError().message;

  ASSERT_TRUE(folders_service_->SoftDelete(Owner(), folder.Value().id).Ok());

  // 子树文件夹与其中的文件都进回收站
  auto trash_list = trash_->List(Owner(), Pagination{}, "");
  ASSERT_TRUE(trash_list.Ok());
  EXPECT_EQ(trash_list.Value().total, 3U) << "docs + inner + deep.bin";
  auto files_only = trash_->List(Owner(), Pagination{}, "file");
  ASSERT_TRUE(files_only.Ok());
  EXPECT_EQ(files_only.Value().total, 1U);
  auto folders_only = trash_->List(Owner(), Pagination{}, "folder");
  ASSERT_TRUE(folders_only.Ok());
  EXPECT_EQ(folders_only.Value().total, 2U);
}

// ---- 恢复 -------------------------------------------------------------------

TEST_F(TrashTest, RestorePutsFileBackAndIsIdempotent) {
  const std::string payload = Repeat('d', 2 * kMiB);
  auto uploaded = Upload(root_id_, "back.bin", payload);
  ASSERT_TRUE(uploaded.Ok()) << uploaded.GetError().message;
  ASSERT_TRUE(trash_->SoftDeleteFile(Owner(), uploaded.Value().id).Ok());

  auto restored = trash_->Restore(Owner(), uploaded.Value().id);
  ASSERT_TRUE(restored.Ok()) << restored.GetError().message;
  EXPECT_FALSE(restored.Value().moved_to_root) << "原目录还在，应放回原处";
  EXPECT_EQ(restored.Value().target_folder_id, root_id_);

  auto metadata = files_service_->GetMetadata(Owner(), uploaded.Value().id);
  ASSERT_TRUE(metadata.Ok()) << "恢复后应能正常访问";
  EXPECT_EQ(metadata.Value().status, FileStatus::kActive);
  EXPECT_FALSE(metadata.Value().deleted_at.has_value());
  EXPECT_EQ(TrashCount(), 0U);

  // 重复恢复：幂等，不报错也不改变状态
  auto again = trash_->Restore(Owner(), uploaded.Value().id);
  ASSERT_TRUE(again.Ok()) << again.GetError().message;
  EXPECT_TRUE(again.Value().already_restored);
  EXPECT_EQ(TrashCount(), 0U);
}

TEST_F(TrashTest, RestoreFallsBackToRootWhenOriginalFolderIsGone) {
  auto folder = folders_service_->Create(Owner(), root_id_, "temp");
  ASSERT_TRUE(folder.Ok());
  auto uploaded = Upload(folder.Value().id, "orphan.bin", Repeat('e', kMiB));
  ASSERT_TRUE(uploaded.Ok()) << uploaded.GetError().message;

  // 删掉整个目录（子树里的文件一并进回收站），再单独恢复那个文件：
  // 此时原目录仍是 TRASHED，不是可用位置，必须落到用户根目录
  ASSERT_TRUE(folders_service_->SoftDelete(Owner(), folder.Value().id).Ok());

  auto restored = trash_->Restore(Owner(), uploaded.Value().id);
  ASSERT_TRUE(restored.Ok()) << restored.GetError().message;
  EXPECT_TRUE(restored.Value().moved_to_root) << "原目录不存在时应回到用户根目录";
  EXPECT_EQ(restored.Value().target_folder_id, root_id_);

  auto metadata = files_service_->GetMetadata(Owner(), uploaded.Value().id);
  ASSERT_TRUE(metadata.Ok());
  EXPECT_EQ(metadata.Value().folder_id, root_id_);
}

TEST_F(TrashTest, RestoreRejectsNameConflictWithExplainableError) {
  const std::string payload = Repeat('f', kMiB);
  auto original = Upload(root_id_, "same.bin", payload);
  ASSERT_TRUE(original.Ok()) << original.GetError().message;

  // 删掉再传一个同名文件，然后尝试恢复旧的
  ASSERT_TRUE(trash_->SoftDeleteFile(Owner(), original.Value().id).Ok());
  auto replacement = Upload(root_id_, "same.bin", Repeat('g', kMiB));
  ASSERT_TRUE(replacement.Ok()) << replacement.GetError().message;

  auto restored = trash_->Restore(Owner(), original.Value().id);
  ASSERT_FALSE(restored.Ok());
  EXPECT_EQ(restored.GetError().code, "RESOURCE_CONFLICT");
  ASSERT_TRUE(restored.GetError().details.count("conflict_name") > 0);
  EXPECT_EQ(restored.GetError().details.at("conflict_name"), "same.bin");

  // 冲突时不能改动任何状态
  EXPECT_EQ(TrashCount(), 1U);
  auto replacement_meta = files_service_->GetMetadata(Owner(), replacement.Value().id);
  EXPECT_TRUE(replacement_meta.Ok());
}

TEST_F(TrashTest, RestoreFolderSubtreeBringsBackEverything) {
  auto folder = folders_service_->Create(Owner(), root_id_, "project");
  ASSERT_TRUE(folder.Ok());
  auto file_a = Upload(folder.Value().id, "a.bin", Repeat('h', kMiB));
  auto file_b = Upload(folder.Value().id, "b.bin", Repeat('i', kMiB));
  ASSERT_TRUE(file_a.Ok());
  ASSERT_TRUE(file_b.Ok());

  ASSERT_TRUE(folders_service_->SoftDelete(Owner(), folder.Value().id).Ok());
  EXPECT_EQ(TrashCount(), 3U);

  auto restored = trash_->Restore(Owner(), folder.Value().id);
  ASSERT_TRUE(restored.Ok()) << restored.GetError().message;
  EXPECT_EQ(TrashCount(), 0U);
  EXPECT_TRUE(files_service_->GetMetadata(Owner(), file_a.Value().id).Ok());
  EXPECT_TRUE(files_service_->GetMetadata(Owner(), file_b.Value().id).Ok());
  auto folders_list = folders_service_->ListChildren(Owner(), root_id_, Pagination{});
  ASSERT_TRUE(folders_list.Ok());
  EXPECT_EQ(folders_list.Value().total, 1U);
}

TEST_F(TrashTest, OtherUsersCannotSeeOrRestore) {
  auto uploaded = Upload(root_id_, "private.bin", Repeat('j', kMiB));
  ASSERT_TRUE(uploaded.Ok());
  ASSERT_TRUE(trash_->SoftDeleteFile(Owner(), uploaded.Value().id).Ok());

  auto other_list = trash_->List(Other(), Pagination{}, "");
  ASSERT_TRUE(other_list.Ok());
  EXPECT_EQ(other_list.Value().total, 0U) << "别人的回收站不可见";

  auto denied = trash_->Restore(Other(), uploaded.Value().id);
  ASSERT_FALSE(denied.Ok());
  EXPECT_EQ(denied.GetError().code, "RESOURCE_NOT_FOUND");
  auto denied_purge = trash_->Purge(Other(), uploaded.Value().id);
  ASSERT_FALSE(denied_purge.Ok());
  EXPECT_EQ(denied_purge.GetError().code, "RESOURCE_NOT_FOUND");
}

// ---- 永久删除与清理任务 ------------------------------------------------------

TEST_F(TrashTest, PurgeRemovesRecordsQueuesObjectsAndFreesQuota) {
  const std::string payload = Repeat('k', 3 * kMiB);
  auto uploaded = Upload(root_id_, "purge.bin", payload);
  ASSERT_TRUE(uploaded.Ok()) << uploaded.GetError().message;
  const std::string key = uploaded.Value().storage_key;
  ASSERT_TRUE(trash_->SoftDeleteFile(Owner(), uploaded.Value().id).Ok());

  auto purged = trash_->Purge(Owner(), uploaded.Value().id);
  ASSERT_TRUE(purged.Ok()) << purged.GetError().message;
  EXPECT_EQ(purged.Value().file_count, 1U);
  EXPECT_EQ(purged.Value().freed_bytes, payload.size());
  EXPECT_EQ(purged.Value().queued_objects, 1U);
  EXPECT_EQ(UsedBytes(owner_.id), 0U) << "永久删除后配额回收";
  EXPECT_EQ(TrashCount(), 0U);

  // 数据库记录已经删掉，但**磁盘对象还没删**（交给后台任务）
  auto metadata = files_service_->GetMetadataIncludingTrashed(Owner(), uploaded.Value().id);
  EXPECT_FALSE(metadata.Ok());
  EXPECT_TRUE(storage_->Exists(key)) << "HTTP 请求不做磁盘删除";

  auto pending = cleanup_repo_->CountByStatus(CleanupStatus::kPending);
  ASSERT_TRUE(pending.Ok());
  EXPECT_EQ(pending.Value(), 1U);

  // 跑一轮清理：对象被删掉，任务标记完成
  cleanup_->RunOnce();
  EXPECT_FALSE(storage_->Exists(key));
  auto after = cleanup_repo_->CountByStatus(CleanupStatus::kPending);
  ASSERT_TRUE(after.Ok());
  EXPECT_EQ(after.Value(), 0U);
  auto done = cleanup_repo_->CountByStatus(CleanupStatus::kDone);
  ASSERT_TRUE(done.Ok());
  EXPECT_EQ(done.Value(), 1U);
}

TEST_F(TrashTest, PurgeFolderRemovesSubtreeAndReclaimsQuota) {
  auto folder = folders_service_->Create(Owner(), root_id_, "bulk");
  ASSERT_TRUE(folder.Ok());
  auto child = folders_service_->Create(Owner(), folder.Value().id, "child");
  ASSERT_TRUE(child.Ok());
  auto file_a = Upload(child.Value().id, "one.bin", Repeat('l', kMiB));
  auto file_b = Upload(child.Value().id, "two.bin", Repeat('m', 2 * kMiB));
  ASSERT_TRUE(file_a.Ok());
  ASSERT_TRUE(file_b.Ok());
  const std::string key_a = file_a.Value().storage_key;
  const std::string key_b = file_b.Value().storage_key;
  const std::uint64_t used_before = UsedBytes(owner_.id);

  ASSERT_TRUE(folders_service_->SoftDelete(Owner(), folder.Value().id).Ok());
  auto purged = trash_->Purge(Owner(), folder.Value().id);
  ASSERT_TRUE(purged.Ok()) << purged.GetError().message;
  EXPECT_EQ(purged.Value().file_count, 2U);
  EXPECT_EQ(purged.Value().folder_count, 2U);
  EXPECT_EQ(purged.Value().queued_objects, 2U);
  EXPECT_EQ(purged.Value().freed_bytes, used_before);
  EXPECT_EQ(UsedBytes(owner_.id), 0U);
  EXPECT_EQ(TrashCount(), 0U);

  cleanup_->RunOnce();
  EXPECT_FALSE(storage_->Exists(key_a));
  EXPECT_FALSE(storage_->Exists(key_b));
}

TEST_F(TrashTest, CleanupKeepsObjectThatIsStillReferenced) {
  // 同一内容被两个文件引用（去重）：永久删除其中一个，对象必须保留
  const std::string payload = Repeat('n', 2 * kMiB);
  auto first = Upload(root_id_, "keep.bin", payload);
  auto second = Upload(root_id_, "drop.bin", payload);
  ASSERT_TRUE(first.Ok());
  ASSERT_TRUE(second.Ok());
  ASSERT_EQ(first.Value().storage_key, second.Value().storage_key);
  const std::string key = first.Value().storage_key;

  ASSERT_TRUE(trash_->SoftDeleteFile(Owner(), second.Value().id).Ok());
  auto purged = trash_->Purge(Owner(), second.Value().id);
  ASSERT_TRUE(purged.Ok()) << purged.GetError().message;

  cleanup_->RunOnce();
  EXPECT_TRUE(storage_->Exists(key)) << "仍被另一个文件引用时不能删物理对象";
  auto stats = cleanup_->stats();
  EXPECT_GE(stats.objects_skipped, 1U);
}

TEST_F(TrashTest, FailedObjectDeletionIsRetriedNotSilentlyMarkedDone) {
  const std::string payload = Repeat('o', kMiB);
  auto uploaded = Upload(root_id_, "retry.bin", payload);
  ASSERT_TRUE(uploaded.Ok());
  const std::string key = uploaded.Value().storage_key;
  ASSERT_TRUE(trash_->SoftDeleteFile(Owner(), uploaded.Value().id).Ok());
  ASSERT_TRUE(trash_->Purge(Owner(), uploaded.Value().id).Ok());

  // 人为制造删除失败：把对象文件先删掉并放一个同名目录占位，delete 会失败
  const auto path = storage_->ResolveKey(key);
  ASSERT_TRUE(path.Ok());
  std::error_code ec;
  std::filesystem::remove(path.Value(), ec);
  std::filesystem::create_directory(path.Value(), ec);
  // 目录非空：std::filesystem::remove 会失败，正好模拟磁盘错误
  std::ofstream blocker(path.Value() / "blocker");
  blocker << "x";
  blocker.close();
  ASSERT_FALSE(ec) << "准备失败场景时出错: " << ec.message();

  cleanup_->RunOnce();

  // 关键：任务不能变成 DONE，数据库状态更不能被当成删除成功
  auto done = cleanup_repo_->CountByStatus(CleanupStatus::kDone);
  ASSERT_TRUE(done.Ok());
  EXPECT_EQ(done.Value(), 0U) << "删除失败不能标记完成";
  auto pending = cleanup_repo_->ListByStatus(CleanupStatus::kPending, 10);
  ASSERT_TRUE(pending.Ok());
  ASSERT_EQ(pending.Value().size(), 1U) << "失败的任务必须留在待处理队列里等重试";
  EXPECT_GE(pending.Value()[0].attempts, 1);
  EXPECT_FALSE(pending.Value()[0].last_error.empty()) << "必须记录失败原因";
  EXPECT_GE(cleanup_->stats().tasks_failed, 1U);

  // 清掉占位目录后重试成功
  std::filesystem::remove_all(path.Value(), ec);
  // 让退避时间立即到期，模拟下次轮询
  ASSERT_TRUE(cleanup_repo_->DeleteAllForTesting().Ok());
  ASSERT_TRUE(cleanup_repo_->Enqueue(key, "purge").Ok());
  cleanup_->RunOnce();
  auto done_after = cleanup_repo_->CountByStatus(CleanupStatus::kDone);
  ASSERT_TRUE(done_after.Ok());
  EXPECT_EQ(done_after.Value(), 1U);
}

TEST_F(TrashTest, ExpiredTrashIsPurgedByBackgroundSweep) {
  const std::string payload = Repeat('p', kMiB);
  auto uploaded = Upload(root_id_, "old.bin", payload);
  ASSERT_TRUE(uploaded.Ok());
  const std::string key = uploaded.Value().storage_key;
  ASSERT_TRUE(trash_->SoftDeleteFile(Owner(), uploaded.Value().id).Ok());

  // 保留期设为 0：刚删除的条目立刻算过期
  auto aggressive = std::make_shared<TrashService>(
      *trash_repo_, *files_, *folders_, *audit_, *cleanup_repo_, *permissions_repo_, *shares_repo_,
      *permissions_, TrashServiceOptions{0});
  auto sweeper =
      std::make_shared<CleanupService>(*cleanup_repo_, *files_, *storage_, *aggressive, *sessions_);
  sweeper->RunOnce(true);

  EXPECT_EQ(aggressive->PurgeExpired(10).Value(), 0U) << "第一轮已经清掉了";
  EXPECT_EQ(TrashCount(), 0U);
  EXPECT_EQ(UsedBytes(owner_.id), 0U);
  sweeper->RunOnce(true);
  EXPECT_FALSE(storage_->Exists(key)) << "过期清理也要把对象交给后台删除";
}

TEST_F(TrashTest, RootFolderCannotBePurged) {
  auto purged = trash_->Purge(Owner(), root_id_);
  // 根目录不是 TRASHED，所以直接是「不在回收站」
  EXPECT_FALSE(purged.Ok());
}

// ---- 审计日志 ---------------------------------------------------------------

TEST_F(TrashTest, DeleteRestoreAndPurgeAreAudited) {
  const std::string payload = Repeat('q', kMiB);
  auto uploaded = Upload(root_id_, "audit.bin", payload);
  ASSERT_TRUE(uploaded.Ok());
  const std::string file_id = uploaded.Value().id;

  ASSERT_TRUE(trash_->SoftDeleteFile(Owner(), file_id).Ok());
  ASSERT_TRUE(trash_->Restore(Owner(), file_id).Ok());
  ASSERT_TRUE(trash_->SoftDeleteFile(Owner(), file_id).Ok());
  ASSERT_TRUE(trash_->Purge(Owner(), file_id).Ok());

  // 删除 → 恢复 → 再删除 → 永久删除：删除记录两条，恢复与永久删除各一条
  const std::pair<const char*, std::uint64_t> expected[] = {
      {"delete_file", 2U}, {"restore_file", 1U}, {"purge_file", 1U}};
  for (const auto& [action, times] : expected) {
    auto count = audit_->CountByAction(action);
    ASSERT_TRUE(count.Ok());
    EXPECT_EQ(count.Value(), times) << "审计记录条数不符: " << action;
  }

  auto entries = audit_->ListRecent(file_id, 10);
  ASSERT_TRUE(entries.Ok());
  EXPECT_EQ(entries.Value().size(), 4U);
  // 审计里必须能看出是谁做的
  for (const auto& entry : entries.Value()) {
    EXPECT_EQ(entry.actor_id, owner_.id);
    EXPECT_FALSE(entry.detail_json.empty());
  }
}

TEST_F(TrashTest, DeletedFileCannotBeDownloadedOrVersioned) {
  const std::string payload = Repeat('r', kMiB);
  auto uploaded = Upload(root_id_, "closed.bin", payload);
  ASSERT_TRUE(uploaded.Ok());
  const std::string file_id = uploaded.Value().id;
  ASSERT_TRUE(trash_->SoftDeleteFile(Owner(), file_id).Ok());

  EXPECT_FALSE(files_service_->PrepareDownload(Owner(), file_id).Ok()) << "回收站文件不能下载";
  EXPECT_FALSE(files_service_->ListVersions(Owner(), file_id).Ok()) << "回收站文件不能看版本";

  // 也不能给已删除的文件加新版本
  CreateUploadRequest request;
  request.folder_id = root_id_;
  request.file_name = "closed.bin";
  request.total_size = kMiB;
  request.chunk_size = kChunk;
  request.target_file_id = file_id;
  auto rejected = uploads_->CreateSession(Owner(), request);
  ASSERT_FALSE(rejected.Ok());
  EXPECT_EQ(rejected.GetError().code, "FILE_NOT_FOUND");
}

TEST_F(TrashTest, ExpiredUploadSessionsAreSweptByCleanup) {
  // 阶段十一：过期上传会话由后台任务落状态并清掉残留分块
  CreateUploadRequest request;
  request.folder_id = root_id_;
  request.file_name = "expired.bin";
  request.total_size = kMiB;
  request.chunk_size = kChunk;
  auto created = uploads_->CreateSession(Owner(), request);
  ASSERT_TRUE(created.Ok()) << created.GetError().message;
  const std::string upload_id = created.Value().session.id;

  auto start = uploads_->BeginChunk(Owner(), upload_id, 0, std::string());
  ASSERT_TRUE(start.Ok());
  const std::string part = Repeat('z', kMiB);
  ASSERT_TRUE(start.Value().session->Write(part.data(), part.size()).Ok());
  ASSERT_TRUE(start.Value().session->Finish().Ok());
  ASSERT_TRUE(sessions_->CountChunks(upload_id).Ok());
  ASSERT_EQ(sessions_->CountChunks(upload_id).Value(), 1U);

  // 把过期时间提前到过去，再跑一轮后台清理
  ASSERT_TRUE(sessions_->ExpireForTesting(upload_id).Ok());
  const auto before = cleanup_->stats().sessions_expired;
  cleanup_->RunOnce(true);

  auto status = uploads_->GetStatus(Owner(), upload_id);
  ASSERT_TRUE(status.Ok());
  EXPECT_EQ(status.Value().session.status, UploadStatus::kExpired) << "过期会话必须落成 EXPIRED";
  EXPECT_EQ(sessions_->CountChunks(upload_id).Value(), 0U) << "残留分块记录必须清掉";
  EXPECT_GE(cleanup_->stats().sessions_expired, before + 1);
}

TEST_F(TrashTest, StartupRecoveryReschedulesPendingTasks) {
  // 阶段十一：失败重试会退避到未来；重启后必须立刻重新排队，不能干等一个退避周期
  const std::string key = "objects/aa/" + std::string(64, 'a');
  ASSERT_TRUE(cleanup_repo_->Enqueue(key, "test-backoff").Ok());

  // 注意：开发机上可能同时跑着服务端，它的后台线程也在 ClaimDue 同一张表。
  // 因此不能对"一次领取到几条"做断言，只能按自己的任务 identity 断言。
  auto claim_mine = [&]() -> std::string {
    for (int round = 0; round < 200; ++round) {
      auto batch = cleanup_repo_->ClaimDue(8);
      if (!batch.Ok() || batch.Value().empty()) {
        return {};
      }
      for (const auto& task : batch.Value()) {
        if (task.storage_key == key) {
          return task.id;
        }
      }
    }
    return {};
  };
  auto drain_all = [&]() {
    for (int round = 0; round < 200; ++round) {
      auto batch = cleanup_repo_->ClaimDue(16);
      if (!batch.Ok() || batch.Value().empty()) {
        return;
      }
    }
  };

  const std::string task_id = claim_mine();
  ASSERT_FALSE(task_id.empty()) << "自己的任务必须先被领取到（可能被别的线程抢走了）";

  // 标记失败 → 退避到未来（仍然是 PENDING，但不到期）
  ASSERT_TRUE(cleanup_repo_->MarkFailed(task_id, "boom", 5).Ok());
  drain_all();  // 把别人留下的到期任务领干净，剩下的都不该到期
  auto not_due = cleanup_repo_->ClaimDue(16);
  ASSERT_TRUE(not_due.Ok());
  for (const auto& task : not_due.Value()) {
    EXPECT_NE(task.id, task_id) << "退避期间自己的任务不该被领取";
  }

  // 模拟服务重启：待处理任务立即到期
  EXPECT_TRUE(cleanup_repo_->ResetPendingForStartup().Ok());
  const std::string retried_id = claim_mine();
  EXPECT_EQ(retried_id, task_id) << "启动恢复后必须可以立即重试自己的任务";
}

TEST_F(TrashTest, ConsistencyScanFindsOrphansButNeverDeletesReferencedFiles) {
  // 一个正常文件：对象被引用，扫描不该动它
  auto uploaded = Upload(root_id_, "kept.bin", Repeat('y', kMiB));
  ASSERT_TRUE(uploaded.Ok()) << uploaded.GetError().message;
  const std::string key = uploaded.Value().storage_key;

  // 人为造一个"没人引用的孤儿对象"
  const auto orphan_hash = std::string(64, 'f');
  const auto orphan_key = "objects/ff/" + orphan_hash;
  auto orphan_path = storage_->ResolveKey(orphan_key);
  ASSERT_TRUE(orphan_path.Ok());
  std::error_code mkdir_ec;
  std::filesystem::create_directories(orphan_path.Value().parent_path(), mkdir_ec);
  ASSERT_FALSE(mkdir_ec) << "准备场景失败: " << mkdir_ec.message();
  {
    std::ofstream out(orphan_path.Value(), std::ios::binary);
    out << "orphan";
    ASSERT_TRUE(out.good()) << "孤儿对象没写成功，测试场景无效";
  }

  cleanup_->RunOnce(true);

  // 孤儿对象被入队清理 → 下一轮真的删掉
  cleanup_->RunOnce();
  EXPECT_FALSE(storage_->Exists(orphan_key)) << "无引用对象应被回收";
  EXPECT_TRUE(storage_->Exists(key)) << "仍被引用的对象绝不能删";
  EXPECT_GE(cleanup_->stats().orphan_objects, 1U);

  // 反过来：数据库有引用但磁盘对象缺失 → 只告警，绝不改数据库（红线）
  std::error_code ec;
  const auto kept_path = storage_->ResolveKey(key);
  ASSERT_TRUE(kept_path.Ok());
  std::filesystem::remove(kept_path.Value(), ec);
  ASSERT_FALSE(ec);
  cleanup_->RunOnce(true);
  EXPECT_GE(cleanup_->stats().missing_objects, 1U) << "缺失对象必须被发现并告警";
  auto metadata = files_service_->GetMetadataIncludingTrashed(Owner(), uploaded.Value().id);
  EXPECT_TRUE(metadata.Ok()) << "缺对象时数据库记录必须原样保留，不能自动删除";
}

TEST_F(TrashTest, StorageStatsRecalibrationFixesDrift) {
  // 阶段十一：used_storage_bytes 只是缓存，任何漏加漏减都会长期存在，必须能定期校准
  const std::string payload = Repeat('w', 2 * kMiB);
  auto uploaded = Upload(root_id_, "counted.bin", payload);
  ASSERT_TRUE(uploaded.Ok()) << uploaded.GetError().message;
  const std::uint64_t actual = UsedBytes(owner_.id);
  ASSERT_EQ(actual, payload.size());

  // 人为制造偏差（模拟某条路径漏记/多记）
  ASSERT_TRUE(users_->AddUsedStorage(owner_.id, 4096).Ok());
  ASSERT_EQ(UsedBytes(owner_.id), actual + 4096);

  // 把校准周期设成 1，跑一轮即触发
  CleanupServiceOptions options;
  options.stats_recalibrate_every_ticks = 1;
  auto service = std::make_shared<CleanupService>(*cleanup_repo_, *files_, *storage_, *trash_,
                                                  *sessions_, options);
  service->RunOnce();

  EXPECT_EQ(UsedBytes(owner_.id), actual) << "校准必须把统计改回真实值";
  EXPECT_EQ(UsedBytes(owner_.id), files_->SumSizeByOwner(owner_.id).Value())
      << "校准后的值必须与按版本汇总的口径一致";
  EXPECT_GE(service->stats().stats_corrected_users, 1U) << "修正数量应作为偏差信号被记录";

  // 没有偏差时不应再改任何行：再跑一轮，修正数量不再增长
  const auto corrected_after_first = service->stats().stats_corrected_users;
  service->RunOnce();
  EXPECT_EQ(service->stats().stats_corrected_users, corrected_after_first)
      << "统计已经对齐后再校准不应产生新的修正";
}

}  // namespace
}  // namespace lan_file_hub
