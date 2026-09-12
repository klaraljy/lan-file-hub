// 阶段五分块上传集成测试：需要真实 PostgreSQL（会话与分块状态必须持久化）。
// 对象存储用临时目录，不碰项目里的 storage/。
//
// 覆盖任务书要求的场景：重复分块、缺失分块、中断后恢复、哈希不匹配、并发完成、
// 过期会话、所有者校验、并发上限，以及失败路径必须清理磁盘分块。
#include <algorithm>
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
constexpr std::uint32_t kChunk = 1U * kMiB;  // 测试里用最小允许分块，跑得快

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

class UploadFlowTest : public ::testing::Test {
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
    // users → upload_sessions → upload_chunks 都是 ON DELETE CASCADE，清用户即可
    if (!sessions_->DeleteAllForTesting().Ok()) {
      Skip("upload_sessions 表不可用，请先运行 lan_file_hub_migrate");
      return;
    }

    auto storage = LocalObjectStorage::Create(storage_dir_.Path());
    ASSERT_TRUE(storage.Ok()) << storage.GetError().message;
    storage_ = storage.Value();

    Options().max_concurrent_uploads = 32;
    Options().max_concurrent_uploads_per_user = 4;
    Options().scanner = scanner_;  // 默认空 = 不扫描；子类可以注入假扫描器
    uploads_ = std::make_shared<UploadService>(*sessions_, *files_, *folders_, *users_, *storage_,
                                               *permissions_, Options());

    folders_service_ = std::make_shared<FolderService>(*folders_, *permissions_);

    owner_ = MakeUser("uploadowner");
    other_ = MakeUser("uploadother");
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
  }

  void Skip(const std::string& reason) {
    if (DatabaseRequired()) {
      FAIL() << reason << "（但设置了 LAN_FILE_HUB_REQUIRE_DB=1）";
    }
    GTEST_SKIP() << "跳过：" << reason;
  }

  UploadServiceOptions& Options() { return options_; }

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

  Result<UploadStatusView> CreateSession(const std::string& name, const std::string& payload,
                                         const std::string& folder_id, bool declare_hash = true) {
    CreateUploadRequest request;
    request.folder_id = folder_id;
    request.file_name = name;
    request.mime_type = "application/octet-stream";
    request.total_size = payload.size();
    request.chunk_size = kChunk;
    if (declare_hash) {
      request.sha256 = Sha256Hex(payload);
    }
    return uploads_->CreateSession(Owner(), request);
  }

  // 上传一个分块：走 BeginChunk → Write → Finish，和控制器一样的路径
  Status PutChunk(const std::string& upload_id, std::uint32_t index, const std::string& data,
                  bool declare_hash = true) {
    auto start = uploads_->BeginChunk(Owner(), upload_id, index,
                                      declare_hash ? Sha256Hex(data) : std::string());
    if (!start) {
      return Status(start.GetError());
    }
    if (start.Value().already_present) {
      return Status::Success();
    }
    auto written = start.Value().session->Write(data.data(), data.size());
    if (!written.Ok()) {
      return written;
    }
    return start.Value().session->Finish();
  }

  // 把 payload 切成 kMiB 大小的分块，返回每块内容
  std::vector<std::string> Split(const std::string& payload) const {
    std::vector<std::string> parts;
    for (std::size_t offset = 0; offset < payload.size(); offset += kChunk) {
      parts.push_back(
          payload.substr(offset, std::min<std::size_t>(kChunk, payload.size() - offset)));
    }
    if (parts.empty()) {
      parts.emplace_back();
    }
    return parts;
  }

  std::uint64_t ChunkRows(const std::string& upload_id) {
    auto count = sessions_->CountChunks(upload_id);
    return count.Ok() ? count.Value() : 0;
  }

  bool ChunkDirExists(const std::string& upload_id) {
    return std::filesystem::exists(storage_dir_.Path() / "tmp" / "uploads" / upload_id);
  }

  testing::TempDir storage_dir_{"upload_storage"};
  std::shared_ptr<ConnectionPool> pool_;
  std::shared_ptr<UserRepository> users_;
  std::shared_ptr<FolderRepository> folders_;
  std::shared_ptr<FileRepository> files_;
  std::shared_ptr<UploadSessionRepository> sessions_;
  std::shared_ptr<OwnerBasedPermissionChecker> permissions_;
  std::shared_ptr<LocalObjectStorage> storage_;
  std::shared_ptr<UploadService> uploads_;
  std::shared_ptr<FolderService> folders_service_;
  User owner_;
  User other_;
  std::string root_id_;
  // 内容安全扫描（阶段十二第 17 条）：子类在 SetUp 里注入假实现
  std::shared_ptr<FileScanner> scanner_;

 private:
  UploadServiceOptions options_;
};

// ---- 正常路径 ---------------------------------------------------------------

TEST_F(UploadFlowTest, ChunkedUploadVerifiesHashAndBuildsContentAddressedObject) {
  const std::string payload = Repeat('a', 3 * kChunk + 123);  // 4 块（最后一块不满）
  auto created = CreateSession("chunked.bin", payload, root_id_);
  ASSERT_TRUE(created.Ok()) << created.GetError().message;
  const std::string upload_id = created.Value().session.id;
  EXPECT_EQ(created.Value().session.chunk_size, kChunk);
  EXPECT_EQ(created.Value().session.total_chunks, 4U);
  EXPECT_EQ(created.Value().session.status, UploadStatus::kInitiated);
  EXPECT_FALSE(created.Value().session.expires_at.empty()) << "会话必须带过期时间";

  // 乱序上传：分块不保证按顺序到达
  const auto parts = Split(payload);
  for (const std::uint32_t index : {2U, 0U, 3U, 1U}) {
    ASSERT_TRUE(PutChunk(upload_id, index, parts[index]).Ok()) << "分块 " << index;
  }

  auto status = uploads_->GetStatus(Owner(), upload_id);
  ASSERT_TRUE(status.Ok());
  EXPECT_EQ(status.Value().completed_chunks.size(), 4U);
  EXPECT_EQ(status.Value().received_bytes, payload.size());

  auto completed = uploads_->Complete(Owner(), upload_id);
  ASSERT_TRUE(completed.Ok()) << completed.GetError().message;
  EXPECT_EQ(completed.Value().content_hash, Sha256Hex(payload)) << "哈希由服务端重算";
  EXPECT_EQ(completed.Value().size_bytes, payload.size());
  EXPECT_EQ(completed.Value().storage_key,
            "objects/" + Sha256Hex(payload).substr(0, 2) + "/" + Sha256Hex(payload));
  // 完成后分块必须清干净（数据库与磁盘）
  EXPECT_EQ(ChunkRows(upload_id), 0U);
  EXPECT_FALSE(ChunkDirExists(upload_id));
}

TEST_F(UploadFlowTest, CompleteIsIdempotent) {
  const std::string payload = Repeat('b', kChunk + 7);
  auto created = CreateSession("idem.bin", payload, root_id_);
  ASSERT_TRUE(created.Ok());
  const std::string upload_id = created.Value().session.id;
  const auto parts = Split(payload);
  for (std::uint32_t i = 0; i < parts.size(); ++i) {
    ASSERT_TRUE(PutChunk(upload_id, i, parts[i]).Ok());
  }

  auto first = uploads_->Complete(Owner(), upload_id);
  ASSERT_TRUE(first.Ok()) << first.GetError().message;
  auto second = uploads_->Complete(Owner(), upload_id);
  ASSERT_TRUE(second.Ok()) << "重复完成必须幂等成功";
  EXPECT_EQ(first.Value().id, second.Value().id);
}

// ---- 重复分块 ---------------------------------------------------------------

TEST_F(UploadFlowTest, DuplicateChunkWithSameContentIsIdempotent) {
  const std::string payload = Repeat('c', 2 * kChunk);
  auto created = CreateSession("dup.bin", payload, root_id_);
  ASSERT_TRUE(created.Ok());
  const std::string upload_id = created.Value().session.id;
  const auto parts = Split(payload);

  ASSERT_TRUE(PutChunk(upload_id, 0, parts[0]).Ok());
  ASSERT_TRUE(PutChunk(upload_id, 0, parts[0]).Ok()) << "同内容重复上传必须成功";
  EXPECT_EQ(ChunkRows(upload_id), 1U) << "重复分块不能产生第二条记录";

  // 第二次应当被识别为「已存在」，不再写盘
  auto again = uploads_->BeginChunk(Owner(), upload_id, 0, Sha256Hex(parts[0]));
  ASSERT_TRUE(again.Ok());
  EXPECT_TRUE(again.Value().already_present);
}

TEST_F(UploadFlowTest, DuplicateChunkWithDifferentContentConflicts) {
  const std::string payload = Repeat('d', 2 * kChunk);
  auto created = CreateSession("conflict.bin", payload, root_id_);
  ASSERT_TRUE(created.Ok());
  const std::string upload_id = created.Value().session.id;
  const auto parts = Split(payload);
  ASSERT_TRUE(PutChunk(upload_id, 0, parts[0]).Ok());

  // 同一索引塞不同内容：客户端声明的哈希与已存记录不符
  const std::string intruder = Repeat('z', kChunk);
  auto start = uploads_->BeginChunk(Owner(), upload_id, 0, Sha256Hex(intruder));
  ASSERT_FALSE(start.Ok());
  EXPECT_EQ(start.GetError().code, "CHUNK_CONFLICT");

  // 已存分块不能被破坏：原内容补齐后仍能正常完成
  EXPECT_EQ(ChunkRows(upload_id), 1U);
  ASSERT_TRUE(PutChunk(upload_id, 1, parts[1]).Ok());
  auto completed = uploads_->Complete(Owner(), upload_id);
  ASSERT_TRUE(completed.Ok()) << completed.GetError().message;
  EXPECT_EQ(completed.Value().content_hash, Sha256Hex(payload));
}

// ---- 缺失分块与断点续传 -----------------------------------------------------

TEST_F(UploadFlowTest, MissingChunkBlocksCompletionButKeepsSessionUsable) {
  const std::string payload = Repeat('e', 3 * kChunk);
  auto created = CreateSession("missing.bin", payload, root_id_);
  ASSERT_TRUE(created.Ok());
  const std::string upload_id = created.Value().session.id;
  const auto parts = Split(payload);
  ASSERT_TRUE(PutChunk(upload_id, 0, parts[0]).Ok());

  auto completed = uploads_->Complete(Owner(), upload_id);
  ASSERT_FALSE(completed.Ok());
  EXPECT_EQ(completed.GetError().code, "UPLOAD_INCOMPLETE");

  // 拒绝之后会话必须还能继续用（否则「断点续传」就无从谈起）
  auto status = uploads_->GetStatus(Owner(), upload_id);
  ASSERT_TRUE(status.Ok());
  EXPECT_TRUE(IsInProgress(status.Value().session.status));
  EXPECT_EQ(status.Value().completed_chunks.size(), 1U);
}

TEST_F(UploadFlowTest, ResumesAfterInterruptionWithFreshServiceInstance) {
  const std::string payload = Repeat('f', 3 * kChunk);
  auto created = CreateSession("resume.bin", payload, root_id_);
  ASSERT_TRUE(created.Ok());
  const std::string upload_id = created.Value().session.id;
  const auto parts = Split(payload);
  ASSERT_TRUE(PutChunk(upload_id, 0, parts[0]).Ok());

  // 模拟服务重启：全新的仓储、服务与存储实例，状态只能来自 PostgreSQL
  auto fresh_sessions = std::make_shared<UploadSessionRepository>(*pool_);
  auto fresh_storage = LocalObjectStorage::Create(storage_dir_.Path());
  ASSERT_TRUE(fresh_storage.Ok());
  UploadService restarted(*fresh_sessions, *files_, *folders_, *users_, *fresh_storage.Value(),
                          *permissions_, Options());

  auto status = restarted.GetStatus(Owner(), upload_id);
  ASSERT_TRUE(status.Ok()) << "重启后必须能读到会话";
  ASSERT_EQ(status.Value().completed_chunks.size(), 1U);
  EXPECT_EQ(status.Value().completed_chunks[0], 0U);

  // 只补缺失的分块
  for (std::uint32_t i = 1; i < parts.size(); ++i) {
    auto start = restarted.BeginChunk(Owner(), upload_id, i, Sha256Hex(parts[i]));
    ASSERT_TRUE(start.Ok()) << start.GetError().message;
    ASSERT_FALSE(start.Value().already_present) << "缺失分块不该被当成已存在";
    ASSERT_TRUE(start.Value().session->Write(parts[i].data(), parts[i].size()).Ok());
    ASSERT_TRUE(start.Value().session->Finish().Ok());
  }

  auto completed = restarted.Complete(Owner(), upload_id);
  ASSERT_TRUE(completed.Ok()) << completed.GetError().message;
  EXPECT_EQ(completed.Value().content_hash, Sha256Hex(payload));
}

// ---- 哈希校验 ---------------------------------------------------------------

TEST_F(UploadFlowTest, DeclaredHashMismatchFailsSessionAndCleansChunks) {
  const std::string payload = Repeat('g', 2 * kChunk);
  CreateUploadRequest request;
  request.folder_id = root_id_;
  request.file_name = "badhash.bin";
  request.total_size = payload.size();
  request.chunk_size = kChunk;
  request.sha256 = std::string(64, 'b');  // 故意声明错的哈希

  auto created = uploads_->CreateSession(Owner(), request);
  ASSERT_TRUE(created.Ok());
  const std::string upload_id = created.Value().session.id;
  const auto parts = Split(payload);
  for (std::uint32_t i = 0; i < parts.size(); ++i) {
    ASSERT_TRUE(PutChunk(upload_id, i, parts[i]).Ok());
  }

  auto completed = uploads_->Complete(Owner(), upload_id);
  ASSERT_FALSE(completed.Ok());
  EXPECT_EQ(completed.GetError().code, "HASH_MISMATCH");

  auto status = uploads_->GetStatus(Owner(), upload_id);
  ASSERT_TRUE(status.Ok());
  EXPECT_EQ(status.Value().session.status, UploadStatus::kFailed);
  EXPECT_EQ(status.Value().session.error_code.value_or(""), "HASH_MISMATCH");
  // 失败不能留下垃圾：数据库记录与磁盘分块都要清掉
  EXPECT_EQ(ChunkRows(upload_id), 0U);
  EXPECT_FALSE(ChunkDirExists(upload_id)) << "失败后磁盘分块必须删除";
  // 也不能生成正式文件
  auto listing =
      files_->ListByFolder(root_id_, Pagination{}, std::string("lower(f.original_name) ASC"));
  ASSERT_TRUE(listing.Ok());
  EXPECT_TRUE(listing.Value().empty());
}

TEST_F(UploadFlowTest, ChunkHashMismatchIsRejectedWithoutStoringChunk) {
  const std::string payload = Repeat('h', 2 * kChunk);
  auto created = CreateSession("chunkhash.bin", payload, root_id_);
  ASSERT_TRUE(created.Ok());
  const std::string upload_id = created.Value().session.id;
  const auto parts = Split(payload);

  // 声明一个与实际内容不符的分块哈希
  auto start = uploads_->BeginChunk(Owner(), upload_id, 0, std::string(64, 'a'));
  ASSERT_TRUE(start.Ok());
  ASSERT_TRUE(start.Value().session->Write(parts[0].data(), parts[0].size()).Ok());
  auto finished = start.Value().session->Finish();
  ASSERT_FALSE(finished.Ok());
  EXPECT_EQ(finished.GetError().code, "CHUNK_INVALID");
  EXPECT_EQ(ChunkRows(upload_id), 0U) << "校验失败的分块不能落库";
}

// ---- 并发完成 ---------------------------------------------------------------

TEST_F(UploadFlowTest, ConcurrentCompleteCreatesExactlyOneFile) {
  const std::string payload = Repeat('i', 2 * kChunk);
  auto created = CreateSession("race.bin", payload, root_id_);
  ASSERT_TRUE(created.Ok());
  const std::string upload_id = created.Value().session.id;
  const auto parts = Split(payload);
  for (std::uint32_t i = 0; i < parts.size(); ++i) {
    ASSERT_TRUE(PutChunk(upload_id, i, parts[i]).Ok());
  }

  std::atomic<int> succeeded{0};
  std::atomic<int> conflicted{0};
  std::vector<std::string> file_ids(4);
  std::vector<std::thread> workers;
  workers.reserve(4);
  for (int i = 0; i < 4; ++i) {
    workers.emplace_back([&, i]() {
      auto result = uploads_->Complete(Owner(), upload_id);
      if (result.Ok()) {
        file_ids[static_cast<std::size_t>(i)] = result.Value().id;
        succeeded.fetch_add(1);
      } else {
        // 没抢到合并权的请求必须得到可解释的冲突，不能是内部错误
        EXPECT_TRUE(result.GetError().code == "RESOURCE_CONFLICT" ||
                    result.GetError().code == "UPLOAD_ABORTED" ||
                    result.GetError().code == "STORAGE_UNAVAILABLE")
            << result.GetError().code;
        conflicted.fetch_add(1);
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }

  EXPECT_GE(succeeded.load(), 1);
  EXPECT_EQ(succeeded.load() + conflicted.load(), 4);
  // 所有成功的响应必须指向同一个文件
  std::string first;
  for (const auto& id : file_ids) {
    if (id.empty()) {
      continue;
    }
    if (first.empty()) {
      first = id;
    }
    EXPECT_EQ(id, first) << "并发完成不能生成多个文件";
  }

  auto listing =
      files_->ListByFolder(root_id_, Pagination{}, std::string("lower(f.original_name) ASC"));
  ASSERT_TRUE(listing.Ok());
  ASSERT_EQ(listing.Value().size(), 1U) << "同名文件只能有一条记录";
  EXPECT_EQ(listing.Value()[0].content_hash, Sha256Hex(payload));
}

// ---- 过期、取消、权限、上限 -------------------------------------------------

TEST_F(UploadFlowTest, ExpiredSessionRejectsFurtherChunks) {
  const std::string payload = Repeat('j', 2 * kChunk);
  auto created = CreateSession("expire.bin", payload, root_id_);
  ASSERT_TRUE(created.Ok());
  const std::string upload_id = created.Value().session.id;
  ASSERT_TRUE(sessions_->ExpireForTesting(upload_id).Ok());

  auto status = uploads_->GetStatus(Owner(), upload_id);
  ASSERT_TRUE(status.Ok());
  EXPECT_EQ(status.Value().session.status, UploadStatus::kExpired);

  auto start = uploads_->BeginChunk(Owner(), upload_id, 0, std::string());
  ASSERT_FALSE(start.Ok());
  EXPECT_EQ(start.GetError().code, "UPLOAD_EXPIRED");

  auto completed = uploads_->Complete(Owner(), upload_id);
  ASSERT_FALSE(completed.Ok());
  EXPECT_EQ(completed.GetError().code, "UPLOAD_EXPIRED");
}

TEST_F(UploadFlowTest, CancelIsIdempotentAndCleansChunks) {
  const std::string payload = Repeat('k', 2 * kChunk);
  auto created = CreateSession("cancel.bin", payload, root_id_);
  ASSERT_TRUE(created.Ok());
  const std::string upload_id = created.Value().session.id;
  const auto parts = Split(payload);
  ASSERT_TRUE(PutChunk(upload_id, 0, parts[0]).Ok());
  ASSERT_TRUE(ChunkDirExists(upload_id));

  ASSERT_TRUE(uploads_->Cancel(Owner(), upload_id).Ok());
  EXPECT_TRUE(uploads_->Cancel(Owner(), upload_id).Ok()) << "重复取消必须幂等";
  EXPECT_EQ(ChunkRows(upload_id), 0U);
  EXPECT_FALSE(ChunkDirExists(upload_id));

  auto start = uploads_->BeginChunk(Owner(), upload_id, 1, std::string());
  ASSERT_FALSE(start.Ok());
  EXPECT_EQ(start.GetError().code, "UPLOAD_ABORTED");
}

TEST_F(UploadFlowTest, OtherUsersCannotTouchTheSession) {
  const std::string payload = Repeat('l', 2 * kChunk);
  auto created = CreateSession("private.bin", payload, root_id_);
  ASSERT_TRUE(created.Ok());
  const std::string upload_id = created.Value().session.id;
  const auto parts = Split(payload);
  ASSERT_TRUE(PutChunk(upload_id, 0, parts[0]).Ok());

  // 别人的会话一律当作不存在（404），不泄露「存在但无权限」
  auto status = uploads_->GetStatus(Other(), upload_id);
  ASSERT_FALSE(status.Ok());
  EXPECT_EQ(status.GetError().code, "UPLOAD_NOT_FOUND");

  auto start = uploads_->BeginChunk(Other(), upload_id, 1, std::string());
  ASSERT_FALSE(start.Ok());
  EXPECT_EQ(start.GetError().code, "UPLOAD_NOT_FOUND");

  auto completed = uploads_->Complete(Other(), upload_id);
  ASSERT_FALSE(completed.Ok());
  EXPECT_EQ(completed.GetError().code, "UPLOAD_NOT_FOUND");

  EXPECT_FALSE(uploads_->Cancel(Other(), upload_id).Ok());
  // 会话仍然属于原主人，且没有被别人动过
  auto still_there = uploads_->GetStatus(Owner(), upload_id);
  ASSERT_TRUE(still_there.Ok());
  EXPECT_EQ(still_there.Value().session.status, UploadStatus::kInitiated);
  EXPECT_EQ(still_there.Value().completed_chunks.size(), 1U);
}

TEST_F(UploadFlowTest, IndexOutOfRangeIsRejected) {
  const std::string payload = Repeat('m', 2 * kChunk);
  auto created = CreateSession("index.bin", payload, root_id_);
  ASSERT_TRUE(created.Ok());
  const std::string upload_id = created.Value().session.id;

  for (const std::uint32_t index : {2U, 99U}) {
    auto start = uploads_->BeginChunk(Owner(), upload_id, index, std::string());
    ASSERT_FALSE(start.Ok()) << "索引 " << index << " 应越界";
    EXPECT_EQ(start.GetError().code, "CHUNK_INVALID");
  }
}

TEST_F(UploadFlowTest, PerUserConcurrencyLimitBlocksNewSessions) {
  Options().max_concurrent_uploads_per_user = 2;
  auto uploads = std::make_shared<UploadService>(*sessions_, *files_, *folders_, *users_, *storage_,
                                                 *permissions_, Options());

  const std::string payload = Repeat('n', kChunk);
  for (int i = 0; i < 2; ++i) {
    CreateUploadRequest request;
    request.folder_id = root_id_;
    request.file_name = "limit" + std::to_string(i) + ".bin";
    request.total_size = payload.size();
    request.chunk_size = kChunk;
    ASSERT_TRUE(uploads->CreateSession(Owner(), request).Ok()) << "第 " << i << " 个会话应成功";
  }

  CreateUploadRequest over;
  over.folder_id = root_id_;
  over.file_name = "over.bin";
  over.total_size = payload.size();
  over.chunk_size = kChunk;
  auto rejected = uploads->CreateSession(Owner(), over);
  ASSERT_FALSE(rejected.Ok());
  EXPECT_EQ(rejected.GetError().code, "STORAGE_BUSY");

  // 另一个用户不受影响（限制是按用户算的）：在自己的根目录下照样能建会话
  auto other_root = folders_service_->EnsureRootFolder(Other());
  ASSERT_TRUE(other_root.Ok()) << other_root.GetError().message;
  over.folder_id = other_root.Value().id;
  auto other_session = uploads->CreateSession(Other(), over);
  ASSERT_TRUE(other_session.Ok()) << other_session.GetError().message;
}

TEST_F(UploadFlowTest, RejectsUnsafeFileNamesAndSizes) {
  CreateUploadRequest request;
  request.folder_id = root_id_;
  request.chunk_size = kChunk;
  request.total_size = 1024;

  request.file_name = "../escape.txt";
  EXPECT_FALSE(uploads_->CreateSession(Owner(), request).Ok()) << "文件名不得含路径分隔";

  request.file_name = "ok.bin";
  request.total_size = Options().max_file_size_bytes + 1;
  auto too_big = uploads_->CreateSession(Owner(), request);
  ASSERT_FALSE(too_big.Ok());
  EXPECT_EQ(too_big.GetError().code, "INVALID_ARGUMENT");

  request.total_size = 1024;
  request.sha256 = "not-a-hash";
  auto bad_hash = uploads_->CreateSession(Owner(), request);
  ASSERT_FALSE(bad_hash.Ok());
  EXPECT_EQ(bad_hash.GetError().code, "INVALID_ARGUMENT");

  // 不存在的目录
  request.sha256.reset();
  request.folder_id = GenerateUuidV4();
  auto missing = uploads_->CreateSession(Owner(), request);
  ASSERT_FALSE(missing.Ok());
  EXPECT_EQ(missing.GetError().code, "FOLDER_NOT_FOUND");
}

// ---- 内容安全扫描（阶段十二第 17 条）----------------------------------------

namespace {

// 假扫描器：本期的 NoopFileScanner 永远返回 skipped，没法验证"带毒内容被拦住"这条路径，
// 所以用一个可控的假实现把两条分支都测到。
class FakeScanner : public FileScanner {
 public:
  explicit FakeScanner(ScanVerdict verdict, bool fail = false) : verdict_(verdict), fail_(fail) {}

  Result<ScanResult> Scan(std::string_view storage_key, std::string_view content_hash) override {
    (void)content_hash;
    scanned_keys_.emplace_back(storage_key);
    if (fail_) {
      return Error{"STORAGE_UNAVAILABLE", "扫描服务不可用"};
    }
    ScanResult result;
    result.verdict = verdict_;
    result.detail = "fake scanner";
    return result;
  }

  [[nodiscard]] std::string_view name() const override { return "fake"; }
  [[nodiscard]] const std::vector<std::string>& scanned_keys() const { return scanned_keys_; }

 private:
  ScanVerdict verdict_;
  bool fail_;
  std::vector<std::string> scanned_keys_;
};

}  // namespace

class UploadScanTest : public UploadFlowTest {
 protected:
  void SetUp() override {
    scanner_ = std::make_shared<FakeScanner>(ScanVerdict::kInfected);
    UploadFlowTest::SetUp();
  }

  std::shared_ptr<FakeScanner> Fake() const {
    return std::static_pointer_cast<FakeScanner>(scanner_);
  }
};

TEST_F(UploadScanTest, InfectedContentIsRejectedAndNeverReferenced) {
  const std::string payload = Repeat('m', 4096);
  auto created = CreateSession("malware.bin", payload, root_id_);
  ASSERT_TRUE(created.Ok()) << created.GetError().message;
  const std::string upload_id = created.Value().session.id;
  ASSERT_TRUE(PutChunk(upload_id, 0, payload).Ok());

  auto completed = uploads_->Complete(Owner(), upload_id);
  ASSERT_FALSE(completed.Ok()) << "判定为恶意内容时不能创建文件";
  EXPECT_EQ(completed.GetError().code, "FILE_REJECTED");

  // 对象确实被扫过（保证扫描接在上传完成路径上，而不是摆设）
  ASSERT_EQ(Fake()->scanned_keys().size(), 1U);
  EXPECT_EQ(Fake()->scanned_keys()[0],
            "objects/" + Sha256Hex(payload).substr(0, 2) + "/" + Sha256Hex(payload));

  // 会话按失败收尾，分块被清理；文件表里没有这条记录
  auto status = uploads_->GetStatus(Owner(), upload_id);
  ASSERT_TRUE(status.Ok());
  EXPECT_EQ(status.Value().session.status, UploadStatus::kFailed);
  EXPECT_EQ(ChunkRows(upload_id), 0U);
  EXPECT_FALSE(ChunkDirExists(upload_id));

  // 对象本身已经落在最终位置，但没有任何引用 —— 交给后台清理任务回收（阶段十一）
  EXPECT_TRUE(storage_->Exists(Fake()->scanned_keys()[0])) << "对象按内容寻址，交由后台清理";
  auto referenced = files_->CountObjectReferences(Sha256Hex(payload));
  ASSERT_TRUE(referenced.Ok());
  EXPECT_EQ(referenced.Value(), 0U) << "被拒的对象不能有任何文件引用";
  auto count = files_->CountByFolder(root_id_);
  ASSERT_TRUE(count.Ok());
  EXPECT_EQ(count.Value(), 0U) << "被拒的内容不能出现在目录里";
}

class UploadScanUnavailableTest : public UploadFlowTest {
 protected:
  void SetUp() override {
    scanner_ = std::make_shared<FakeScanner>(ScanVerdict::kClean, /*fail=*/true);
    UploadFlowTest::SetUp();
  }
};

TEST_F(UploadScanUnavailableTest, ScannerFailureBlocksUploadInsteadOfSilentlyPassing) {
  const std::string payload = Repeat('n', 2048);
  auto created = CreateSession("unscannable.bin", payload, root_id_);
  ASSERT_TRUE(created.Ok()) << created.GetError().message;
  const std::string upload_id = created.Value().session.id;
  ASSERT_TRUE(PutChunk(upload_id, 0, payload).Ok());

  auto completed = uploads_->Complete(Owner(), upload_id);
  ASSERT_FALSE(completed.Ok()) << "扫描器故障时必须失败关闭（fail-closed），不能默认放行";
  EXPECT_EQ(completed.GetError().code, "STORAGE_UNAVAILABLE");
}

}  // namespace
}  // namespace lan_file_hub
