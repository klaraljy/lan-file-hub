// 阶段六集成测试：文件去重与用户存储配额。
// 需要真实 PostgreSQL（配额靠事务里的行锁保证，不能靠内存模拟）。
//
// 覆盖：哈希预检查、去重引用、完成时二次去重、配额上限、
//       两个并发上传不能同时绕过配额、引用计数与对象不被误删。
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
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

class DedupQuotaTest : public ::testing::Test {
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

    // 配额给得很小，方便测超限
    owner_ = MakeUser("quotaowner", 8 * kMiB);
    other_ = MakeUser("quotaother", 8 * kMiB);
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
    options.max_concurrent_uploads_per_user = 8;
    return options;
  }

  User MakeUser(const std::string& name, std::uint64_t quota) {
    User user;
    user.id = GenerateUuidV4();
    user.username = name;
    user.email = name + "@example.com";
    user.password_hash = "$argon2id$v=19$m=1024,t=1,p=1$c2FsdA$aGFzaA";
    user.role = UserRole::kUser;
    user.status = UserStatus::kActive;
    user.storage_quota_bytes = quota;
    user.used_storage_bytes = 0;
    return user;
  }

  Actor Owner() const { return Actor{owner_.id, owner_.role}; }
  Actor Other() const { return Actor{other_.id, other_.role}; }

  void SetQuota(const std::string& user_id, std::uint64_t quota) {
    ASSERT_TRUE(users_->SetQuotaForTesting(user_id, quota).Ok());
  }

  std::uint64_t UsedBytes(const std::string& user_id) {
    auto user = users_->FindById(user_id);
    return user.Ok() && user.Value().has_value() ? user.Value()->used_storage_bytes : 0;
  }

  // 走完整的分块上传流程（0 字节文件也按后端的规则传一个空分块）
  Result<File> Upload(const Actor& actor, const std::string& folder_id, const std::string& name,
                      const std::string& payload, bool declare_hash = true) {
    CreateUploadRequest request;
    request.folder_id = folder_id;
    request.file_name = name;
    request.total_size = payload.size();
    request.chunk_size = kChunk;
    if (declare_hash) {
      request.sha256 = Sha256Hex(payload);
    }
    auto created = uploads_->CreateSession(actor, request);
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
      auto start = uploads_->BeginChunk(actor, upload_id, index, Sha256Hex(part));
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
    return uploads_->Complete(actor, upload_id);
  }

  testing::TempDir storage_dir_{"dedup_storage"};
  std::shared_ptr<ConnectionPool> pool_;
  std::shared_ptr<UserRepository> users_;
  std::shared_ptr<FolderRepository> folders_;
  std::shared_ptr<FileRepository> files_;
  std::shared_ptr<UploadSessionRepository> sessions_;
  std::shared_ptr<OwnerBasedPermissionChecker> permissions_;
  std::shared_ptr<LocalObjectStorage> storage_;
  std::shared_ptr<FolderService> folders_service_;
  std::shared_ptr<UploadService> uploads_;
  User owner_;
  User other_;
  std::string root_id_;
};

// ---- 去重 -------------------------------------------------------------------

TEST_F(DedupQuotaTest, SecondUploadOfSameContentReusesTheSameObject) {
  const std::string payload = Repeat('a', 3 * kChunk);

  auto first = Upload(Owner(), root_id_, "first.bin", payload);
  ASSERT_TRUE(first.Ok()) << first.GetError().message;
  const std::string hash = Sha256Hex(payload);
  const std::string key = "objects/" + hash.substr(0, 2) + "/" + hash;
  EXPECT_EQ(first.Value().storage_key, key);

  auto second = Upload(Owner(), root_id_, "second.bin", payload);
  ASSERT_TRUE(second.Ok()) << second.GetError().message;
  EXPECT_EQ(second.Value().storage_key, key) << "相同内容必须指向同一个物理对象";
  EXPECT_NE(first.Value().id, second.Value().id) << "但文件记录是两条";

  // 磁盘上只有一份对象
  auto size = storage_->Size(key);
  ASSERT_TRUE(size.Ok());
  EXPECT_EQ(size.Value(), payload.size());
  EXPECT_EQ(files_->CountObjectReferences(hash).Value(), 2U) << "两个版本引用同一份内容";
}

TEST_F(DedupQuotaTest, CreateSessionReportsExistingObjectForOwnContent) {
  const std::string payload = Repeat('b', 2 * kChunk);
  auto first = Upload(Owner(), root_id_, "origin.bin", payload);
  ASSERT_TRUE(first.Ok()) << first.GetError().message;

  CreateUploadRequest request;
  request.folder_id = root_id_;
  request.file_name = "copy.bin";
  request.total_size = payload.size();
  request.chunk_size = kChunk;
  request.sha256 = Sha256Hex(payload);

  auto created = uploads_->CreateSession(Owner(), request);
  ASSERT_TRUE(created.Ok()) << created.GetError().message;
  EXPECT_TRUE(created.Value().already_exists) << "应提示内容已存在";
  ASSERT_TRUE(created.Value().existing_object.has_value());
  EXPECT_EQ(created.Value().existing_object->file_name, "origin.bin");
  EXPECT_EQ(created.Value().existing_object->size_bytes, payload.size());

  // 一个分块都不用传，直接 complete 就能建出文件
  auto completed = uploads_->Complete(Owner(), created.Value().session.id);
  ASSERT_TRUE(completed.Ok()) << completed.GetError().message;
  EXPECT_EQ(completed.Value().content_hash, Sha256Hex(payload));
  EXPECT_EQ(completed.Value().storage_key,
            "objects/" + Sha256Hex(payload).substr(0, 2) + "/" + Sha256Hex(payload));
}

TEST_F(DedupQuotaTest, DedupPreCheckDoesNotLeakOtherUsersContent) {
  const std::string payload = Repeat('c', 2 * kChunk);
  auto first = Upload(Owner(), root_id_, "private.bin", payload);
  ASSERT_TRUE(first.Ok()) << first.GetError().message;

  auto other_root = folders_service_->EnsureRootFolder(Other());
  ASSERT_TRUE(other_root.Ok());

  CreateUploadRequest request;
  request.folder_id = other_root.Value().id;
  request.file_name = "probe.bin";
  request.total_size = payload.size();
  request.chunk_size = kChunk;
  request.sha256 = Sha256Hex(payload);

  // 别人上传过的内容不能被探测出来（否则上传接口就成了「猜文件是否存在」的工具）
  auto created = uploads_->CreateSession(Other(), request);
  ASSERT_TRUE(created.Ok()) << created.GetError().message;
  EXPECT_FALSE(created.Value().already_exists);
  EXPECT_FALSE(created.Value().existing_object.has_value());

  // 但物理层仍然去重：另一个人传完同样内容，对象只有一份
  auto uploaded = Upload(Other(), other_root.Value().id, "probe.bin", payload);
  ASSERT_TRUE(uploaded.Ok()) << uploaded.GetError().message;
  EXPECT_EQ(uploaded.Value().storage_key, first.Value().storage_key);
}

TEST_F(DedupQuotaTest, QuotaCountsLogicalSizeNotPhysical) {
  const std::string payload = Repeat('d', 3 * kMiB);  // 3 MiB
  auto first = Upload(Owner(), root_id_, "one.bin", payload);
  ASSERT_TRUE(first.Ok()) << first.GetError().message;
  EXPECT_EQ(UsedBytes(owner_.id), 3 * kMiB);

  // 同一用户再引用一次相同内容：物理只存一份，但配额按逻辑大小算两遍
  auto second = Upload(Owner(), root_id_, "two.bin", payload);
  ASSERT_TRUE(second.Ok()) << second.GetError().message;
  EXPECT_EQ(UsedBytes(owner_.id), 6 * kMiB) << "配额按逻辑大小计算（docs/data-model.md）";
}

// ---- 配额 -------------------------------------------------------------------

TEST_F(DedupQuotaTest, UsedStorageIsUpdatedOnUpload) {
  EXPECT_EQ(UsedBytes(owner_.id), 0U);
  const std::string payload = Repeat('e', 2 * kMiB);
  auto uploaded = Upload(Owner(), root_id_, "counted.bin", payload);
  ASSERT_TRUE(uploaded.Ok()) << uploaded.GetError().message;
  EXPECT_EQ(UsedBytes(owner_.id), payload.size());

  // 统计字段必须与按版本汇总的口径一致
  auto sum = files_->SumSizeByOwner(owner_.id);
  ASSERT_TRUE(sum.Ok());
  EXPECT_EQ(sum.Value(), UsedBytes(owner_.id));

  auto recalculated = users_->RecalculateUsedStorage(owner_.id);
  ASSERT_TRUE(recalculated.Ok());
  EXPECT_EQ(recalculated.Value(), payload.size()) << "重算结果必须与增量维护一致";
}

TEST_F(DedupQuotaTest, QuotaIsRejectedAtSessionCreation) {
  SetQuota(owner_.id, 2 * kMiB);

  CreateUploadRequest request;
  request.folder_id = root_id_;
  request.file_name = "toobig.bin";
  request.total_size = 3 * kMiB;
  request.chunk_size = kChunk;

  auto created = uploads_->CreateSession(Owner(), request);
  ASSERT_FALSE(created.Ok());
  EXPECT_EQ(created.GetError().code, "QUOTA_EXCEEDED");
  EXPECT_EQ(UsedBytes(owner_.id), 0U) << "被拒绝的上传不能改变用量";
}

TEST_F(DedupQuotaTest, QuotaIsRecheckedAtCompletion) {
  // 会话创建时配额是够的，但提交前配额被改小（别处传了大文件 / 管理员下调）：
  // 完成阶段必须重新检查，不能因为「创建时通过过」就放行。
  const std::string payload = Repeat('f', 3 * kMiB);
  CreateUploadRequest request;
  request.folder_id = root_id_;
  request.file_name = "late-limit.bin";
  request.total_size = payload.size();
  request.chunk_size = kChunk;
  request.sha256 = Sha256Hex(payload);

  auto created = uploads_->CreateSession(Owner(), request);
  ASSERT_TRUE(created.Ok()) << created.GetError().message;
  const std::string upload_id = created.Value().session.id;

  for (std::uint32_t index = 0; index < 3; ++index) {
    const std::string part = payload.substr(static_cast<std::size_t>(index) * kChunk, kChunk);
    auto start = uploads_->BeginChunk(Owner(), upload_id, index, Sha256Hex(part));
    ASSERT_TRUE(start.Ok());
    ASSERT_TRUE(start.Value().session->Write(part.data(), part.size()).Ok());
    ASSERT_TRUE(start.Value().session->Finish().Ok());
  }

  SetQuota(owner_.id, 2 * kMiB);  // 现在装不下这 3 MiB

  auto completed = uploads_->Complete(Owner(), upload_id);
  ASSERT_FALSE(completed.Ok());
  EXPECT_EQ(completed.GetError().code, "QUOTA_EXCEEDED");
  EXPECT_EQ(UsedBytes(owner_.id), 0U) << "被拒绝的上传不能计入用量";
  // 会话进入 FAILED，分块被清理，不会留下占空间又无法完成的任务
  auto status = uploads_->GetStatus(Owner(), upload_id);
  ASSERT_TRUE(status.Ok());
  EXPECT_EQ(status.Value().session.status, UploadStatus::kFailed);
}

TEST_F(DedupQuotaTest, ConcurrentUploadsCannotBothBypassQuota) {
  // 配额 10 MiB：两个 6 MiB 的上传并发提交，只能成功一个
  SetQuota(owner_.id, 10 * kMiB);
  const std::string payload_a = Repeat('g', 6 * kMiB);
  const std::string payload_b = Repeat('h', 6 * kMiB);

  auto uploads = std::make_shared<UploadService>(*sessions_, *files_, *folders_, *users_, *storage_,
                                                 *permissions_, Options());

  // 先把两个会话的分块都传完，再同时 complete
  std::vector<std::string> upload_ids;
  for (const auto& payload : {payload_a, payload_b}) {
    CreateUploadRequest request;
    request.folder_id = root_id_;
    request.file_name = payload == payload_a ? "race-a.bin" : "race-b.bin";
    request.total_size = payload.size();
    request.chunk_size = kChunk;
    auto created = uploads->CreateSession(Owner(), request);
    ASSERT_TRUE(created.Ok()) << created.GetError().message;
    const std::string upload_id = created.Value().session.id;
    for (std::uint32_t index = 0, offset = 0; offset < payload.size(); ++index) {
      const std::size_t length = std::min<std::size_t>(kChunk, payload.size() - offset);
      const std::string part = payload.substr(offset, length);
      auto start = uploads->BeginChunk(Owner(), upload_id, index, Sha256Hex(part));
      ASSERT_TRUE(start.Ok());
      ASSERT_TRUE(start.Value().session->Write(part.data(), part.size()).Ok());
      ASSERT_TRUE(start.Value().session->Finish().Ok());
      offset += static_cast<std::uint32_t>(length);
    }
    upload_ids.push_back(upload_id);
  }

  std::atomic<int> succeeded{0};
  std::atomic<int> quota_rejected{0};
  std::vector<std::thread> workers;
  for (const auto& upload_id : upload_ids) {
    workers.emplace_back([&, upload_id]() {
      auto result = uploads->Complete(Owner(), upload_id);
      if (result.Ok()) {
        succeeded.fetch_add(1);
      } else if (result.GetError().code == "QUOTA_EXCEEDED") {
        quota_rejected.fetch_add(1);
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }

  EXPECT_EQ(succeeded.load(), 1) << "配额只能容纳一个 6 MiB 文件";
  EXPECT_EQ(quota_rejected.load(), 1) << "另一个必须被配额拒绝，而不是两个都成功";
  EXPECT_LE(UsedBytes(owner_.id), 10 * kMiB) << "用量绝不能超过配额";
  EXPECT_EQ(UsedBytes(owner_.id), 6 * kMiB);
}

TEST_F(DedupQuotaTest, ZeroByteFileDoesNotBreakQuotaAccounting) {
  const std::string payload;
  auto uploaded = Upload(Owner(), root_id_, "empty.bin", payload);
  ASSERT_TRUE(uploaded.Ok()) << uploaded.GetError().message;
  EXPECT_EQ(uploaded.Value().size_bytes, 0U);
  EXPECT_EQ(UsedBytes(owner_.id), 0U);
}

// ---- 引用计数与安全清理 -----------------------------------------------------

TEST_F(DedupQuotaTest, DeletingOneRecordKeepsSharedObject) {
  const std::string payload = Repeat('i', 2 * kMiB);
  auto first = Upload(Owner(), root_id_, "keep.bin", payload);
  ASSERT_TRUE(first.Ok()) << first.GetError().message;
  auto second = Upload(Owner(), root_id_, "drop.bin", payload);
  ASSERT_TRUE(second.Ok()) << second.GetError().message;

  const std::string hash = Sha256Hex(payload);
  const std::string key = first.Value().storage_key;
  EXPECT_EQ(files_->CountObjectReferences(hash).Value(), 2U);

  // 模拟阶段八的删除：删掉一条文件记录（版本随外键级联删除）
  ASSERT_TRUE(files_->DeleteByIdForTesting(second.Value().id).Ok());

  EXPECT_EQ(files_->CountObjectReferences(hash).Value(), 1U);
  EXPECT_TRUE(storage_->Exists(key)) << "还有版本引用时不能删除物理对象";

  // 引用清零后才允许删除对象（阶段八/十一按这个判定执行）
  ASSERT_TRUE(files_->DeleteByIdForTesting(first.Value().id).Ok());
  EXPECT_EQ(files_->CountObjectReferences(hash).Value(), 0U);
  ASSERT_TRUE(storage_->Delete(key).Ok());
  EXPECT_FALSE(storage_->Exists(key));
}

TEST_F(DedupQuotaTest, QuotaIsNotAffectedByFailedUpload) {
  const std::string payload = Repeat('j', 2 * kMiB);
  auto uploaded = Upload(Owner(), root_id_, "ok.bin", payload);
  ASSERT_TRUE(uploaded.Ok()) << uploaded.GetError().message;
  const std::uint64_t before = UsedBytes(owner_.id);

  // 同名文件被拒（RESOURCE_CONFLICT）时不能改变用量
  auto duplicated = Upload(Owner(), root_id_, "ok.bin", Repeat('k', kMiB));
  ASSERT_FALSE(duplicated.Ok());
  EXPECT_EQ(UsedBytes(owner_.id), before);
}

TEST_F(DedupQuotaTest, RejectsUploadWhenDiskReserveIsNotMet) {
  // 阶段十二：磁盘剩余空间检查。把"必须保留的空间"设成 1 PiB，
  // 等价于"磁盘随时都算满"，从而确定性触发 STORAGE_FULL，不用真的塞满磁盘。
  UploadServiceOptions options = Options();
  options.min_free_disk_bytes = 1ULL << 50;
  auto service = std::make_shared<UploadService>(*sessions_, *files_, *folders_, *users_, *storage_,
                                                 *permissions_, options);

  CreateUploadRequest request;
  request.folder_id = root_id_;
  request.file_name = "no-space.bin";
  request.total_size = kMiB;
  request.chunk_size = kChunk;
  auto created = service->CreateSession(Owner(), request);
  ASSERT_FALSE(created.Ok());
  EXPECT_EQ(created.GetError().code, "STORAGE_FULL") << "磁盘空间不足必须在创建会话时就拒绝";
  EXPECT_EQ(UsedBytes(owner_.id), 0U) << "被拒绝的上传不能改变用量";

  // 保留空间设回 0：同样的请求应当成功（证明拦截确实来自磁盘检查）
  options.min_free_disk_bytes = 0;
  auto permissive = std::make_shared<UploadService>(*sessions_, *files_, *folders_, *users_,
                                                    *storage_, *permissions_, options);
  auto ok = permissive->CreateSession(Owner(), request);
  ASSERT_TRUE(ok.Ok()) << ok.GetError().message;
}

}  // namespace
}  // namespace lan_file_hub
