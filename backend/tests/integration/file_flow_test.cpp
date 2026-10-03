// 文件上传下载流程集成测试：需要真实 PostgreSQL。
// 对象存储用临时目录，不碰项目里的 storage/。
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fixtures/temp_dir.h"
#include "lan_file_hub/application/file_service.h"
#include "lan_file_hub/application/folder_service.h"
#include "lan_file_hub/application/permission_checker.h"
#include "lan_file_hub/common/config.h"
#include "lan_file_hub/common/pagination.h"
#include "lan_file_hub/common/sha256.h"
#include "lan_file_hub/common/uuid.h"
#include "lan_file_hub/infrastructure/database.h"
#include "lan_file_hub/infrastructure/file_repository.h"
#include "lan_file_hub/infrastructure/folder_repository.h"
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

std::string ReadAll(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

class FileFlowTest : public ::testing::Test {
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
    files_ = std::make_shared<FileRepository>(*pool_);
    permissions_ = std::make_shared<OwnerBasedPermissionChecker>();

    if (!users_->DeleteAllForTesting().Ok()) {
      Skip("users 表不可用，请先运行 lan_file_hub_migrate");
      return;
    }
    (void)files_->DeleteAllForTesting();

    auto storage = LocalObjectStorage::Create(storage_dir_.Path());
    ASSERT_TRUE(storage.Ok()) << storage.GetError().message;
    storage_ = storage.Value();

    folders_service_ = std::make_shared<FolderService>(*folders_, *files_, *permissions_);
    files_service_ = std::make_shared<FileService>(*files_, *folders_, *storage_, *permissions_,
                                                   FileServiceOptions{64ULL * 1024 * 1024});

    owner_ = MakeUser("fileowner");
    other_ = MakeUser("fileother");
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

  // 走完整的上传流程：BeginUpload → Write → Finish
  Result<File> Upload(const Actor& actor, const std::string& folder_id, const std::string& name,
                      const std::string& payload, std::size_t chunk_size = 0) {
    auto session = files_service_->BeginUpload(actor, UploadRequest{folder_id, name, "text/plain"});
    if (!session) {
      return session.GetError();
    }
    const auto upload = session.Value();

    if (chunk_size == 0) {
      auto written = upload->Write(payload.data(), payload.size());
      if (!written.Ok())
        return written.GetError();
    } else {
      std::size_t offset = 0;
      while (offset < payload.size()) {
        const std::size_t length = std::min(chunk_size, payload.size() - offset);
        auto written = upload->Write(payload.data() + offset, length);
        if (!written.Ok())
          return written.GetError();
        offset += length;
      }
    }
    return upload->Finish();
  }

  testing::TempDir storage_dir_{"file_storage"};
  std::shared_ptr<ConnectionPool> pool_;
  std::shared_ptr<UserRepository> users_;
  std::shared_ptr<FolderRepository> folders_;
  std::shared_ptr<FileRepository> files_;
  std::shared_ptr<OwnerBasedPermissionChecker> permissions_;
  std::shared_ptr<LocalObjectStorage> storage_;
  std::shared_ptr<FolderService> folders_service_;
  std::shared_ptr<FileService> files_service_;
  User owner_;
  User other_;
  std::string root_id_;
};

TEST_F(FileFlowTest, UploadsFileAndComputesServerSideHash) {
  const std::string payload = "hello lan-file-hub";
  const auto uploaded = Upload(Owner(), root_id_, "hello.txt", payload);
  ASSERT_TRUE(uploaded.Ok()) << uploaded.GetError().message;

  EXPECT_EQ(uploaded.Value().original_name, "hello.txt");
  EXPECT_EQ(uploaded.Value().size_bytes, payload.size());
  EXPECT_EQ(uploaded.Value().content_hash, Sha256Hex(payload)) << "哈希必须由服务端计算";
  EXPECT_EQ(uploaded.Value().mime_type, "text/plain");
  EXPECT_FALSE(uploaded.Value().current_version_id.value_or("").empty());
  // 内容寻址：key 由哈希推导，不含原始文件名
  EXPECT_EQ(uploaded.Value().storage_key,
            "objects/" + Sha256Hex(payload).substr(0, 2) + "/" + Sha256Hex(payload));
  EXPECT_EQ(uploaded.Value().storage_key.find("hello"), std::string::npos);
}

TEST_F(FileFlowTest, StreamingInChunksProducesSameHashAsOneShot) {
  std::string payload;
  for (int i = 0; i < 50000; ++i) {
    payload.push_back(static_cast<char>('A' + (i % 26)));
  }

  const auto chunked = Upload(Owner(), root_id_, "chunked.bin", payload, 4096);
  ASSERT_TRUE(chunked.Ok()) << chunked.GetError().message;
  EXPECT_EQ(chunked.Value().content_hash, Sha256Hex(payload));
  EXPECT_EQ(chunked.Value().size_bytes, payload.size());
}

TEST_F(FileFlowTest, DownloadReturnsPathWithOriginalContent) {
  const std::string payload = "download me";
  const auto uploaded = Upload(Owner(), root_id_, "dl.txt", payload);
  ASSERT_TRUE(uploaded.Ok());

  const auto download = files_service_->PrepareDownload(Owner(), uploaded.Value().id);
  ASSERT_TRUE(download.Ok()) << download.GetError().message;
  EXPECT_EQ(download.Value().file.id, uploaded.Value().id);
  EXPECT_EQ(ReadAll(download.Value().absolute_path), payload);
}

TEST_F(FileFlowTest, SecondUploadWithSameContentReusesOneObject) {
  const std::string payload = "shared bytes";
  const auto first = Upload(Owner(), root_id_, "a.txt", payload);
  const auto second = Upload(Owner(), root_id_, "b.txt", payload);
  ASSERT_TRUE(first.Ok());
  ASSERT_TRUE(second.Ok());

  EXPECT_EQ(first.Value().content_hash, second.Value().content_hash);
  EXPECT_EQ(first.Value().storage_key, second.Value().storage_key) << "相同内容必须落到同一个对象";
  EXPECT_NE(first.Value().id, second.Value().id) << "但元数据是两条独立记录";

  // 物理上只有一个对象文件
  std::size_t object_count = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(storage_->root())) {
    if (entry.is_regular_file() && entry.path().parent_path().filename() != "tmp") {
      ++object_count;
    }
  }
  EXPECT_EQ(object_count, 1U);
}

TEST_F(FileFlowTest, RejectsDuplicateNameInSameFolder) {
  ASSERT_TRUE(Upload(Owner(), root_id_, "same.txt", "one").Ok());
  const auto duplicate = Upload(Owner(), root_id_, "SAME.txt", "two");
  ASSERT_FALSE(duplicate.Ok());
  EXPECT_EQ(duplicate.GetError().code, "RESOURCE_CONFLICT");
}

TEST_F(FileFlowTest, RejectsInvalidFileNames) {
  for (const char* name : {"", "   ", ".", "..", "a/b.txt", "a\\b.txt"}) {
    const auto uploaded = Upload(Owner(), root_id_, name, "x");
    EXPECT_FALSE(uploaded.Ok()) << "应拒绝文件名: [" << name << "]";
    EXPECT_EQ(uploaded.GetError().code, "INVALID_ARGUMENT");
  }
}

TEST_F(FileFlowTest, RejectsFileExceedingSizeLimit) {
  // 用一个上限很小的 FileService，避免构造超大缓冲区
  FileService tiny_limit(*files_, *folders_, *storage_, *permissions_, FileServiceOptions{1024});
  auto session = tiny_limit.BeginUpload(Owner(), UploadRequest{root_id_, "big.bin", ""});
  ASSERT_TRUE(session.Ok()) << session.GetError().message;
  const auto upload = session.Value();

  const std::string ok_chunk(512, 'a');
  ASSERT_TRUE(upload->Write(ok_chunk.data(), ok_chunk.size()).Ok());

  const std::string too_much(1024, 'b');
  const auto overflow = upload->Write(too_much.data(), too_much.size());
  ASSERT_FALSE(overflow.Ok()) << "超过单文件上限必须立即失败";
  EXPECT_EQ(overflow.GetError().code, "INVALID_ARGUMENT");
  EXPECT_EQ(upload->bytes_written(), 512U) << "超限的块不应被计入";

  upload->Abort();
}

TEST_F(FileFlowTest, AbortLeavesNoTemporaryObjectOrMetadata) {
  const std::string payload = "will be aborted";
  auto session = files_service_->BeginUpload(Owner(), UploadRequest{root_id_, "abort.txt", ""});
  ASSERT_TRUE(session.Ok());
  const auto upload = session.Value();
  ASSERT_TRUE(upload->Write(payload.data(), payload.size()).Ok());
  upload->Abort();

  // 元数据没有落库：同目录下查不到这个名字
  const auto exists = files_->ExistsWithName(root_id_, "abort.txt");
  ASSERT_TRUE(exists.Ok());
  EXPECT_FALSE(exists.Value()) << "中断的上传不得留下元数据";

  // 临时目录必须干净
  std::size_t temp_files = 0;
  const auto tmp_dir = storage_->root() / "tmp";
  if (std::filesystem::is_directory(tmp_dir)) {
    for (const auto& entry : std::filesystem::directory_iterator(tmp_dir)) {
      if (entry.is_regular_file())
        ++temp_files;
    }
  }
  EXPECT_EQ(temp_files, 0U) << "中断的上传不得留下临时对象";
}

TEST_F(FileFlowTest, OtherUserCannotSeeOrDownloadForeignFile) {
  const auto uploaded = Upload(Owner(), root_id_, "private.txt", "secret");
  ASSERT_TRUE(uploaded.Ok());

  const auto metadata = files_service_->GetMetadata(Other(), uploaded.Value().id);
  ASSERT_FALSE(metadata.Ok());
  EXPECT_EQ(metadata.GetError().code, "RESOURCE_NOT_FOUND") << "不泄露资源是否存在";

  const auto download = files_service_->PrepareDownload(Other(), uploaded.Value().id);
  ASSERT_FALSE(download.Ok());
  EXPECT_EQ(download.GetError().code, "RESOURCE_NOT_FOUND");
}

TEST_F(FileFlowTest, CannotUploadIntoForeignFolder) {
  const auto uploaded = Upload(Other(), root_id_, "planted.txt", "x");
  ASSERT_FALSE(uploaded.Ok());
  EXPECT_EQ(uploaded.GetError().code, "FOLDER_NOT_FOUND");
}

TEST_F(FileFlowTest, ListsOnlyOwnActiveFilesWithPagination) {
  for (const char* name : {"a.txt", "b.txt", "c.txt"}) {
    ASSERT_TRUE(Upload(Owner(), root_id_, name, std::string("content-") + name).Ok());
  }

  Pagination pagination;
  pagination.page_size = 2;
  const auto page1 = files_service_->ListInFolder(Owner(), root_id_, pagination);
  ASSERT_TRUE(page1.Ok());
  EXPECT_EQ(page1.Value().total, 3U);
  EXPECT_EQ(page1.Value().files.size(), 2U);

  pagination.page = 2;
  const auto page2 = files_service_->ListInFolder(Owner(), root_id_, pagination);
  ASSERT_TRUE(page2.Ok());
  EXPECT_EQ(page2.Value().files.size(), 1U);

  const auto foreign = files_service_->ListInFolder(Other(), root_id_, Pagination{});
  EXPECT_FALSE(foreign.Ok()) << "别人的目录不应能列出";
}

TEST_F(FileFlowTest, StoresVersionOneAsCurrent) {
  const auto uploaded = Upload(Owner(), root_id_, "versioned.txt", "v1");
  ASSERT_TRUE(uploaded.Ok());

  const auto versions = files_->ListVersions(uploaded.Value().id);
  ASSERT_TRUE(versions.Ok());
  ASSERT_EQ(versions.Value().size(), 1U);
  EXPECT_EQ(versions.Value()[0].version_number, 1);
  EXPECT_TRUE(versions.Value()[0].is_current);
  EXPECT_EQ(versions.Value()[0].id, uploaded.Value().current_version_id.value_or(""));
  EXPECT_EQ(versions.Value()[0].created_by, owner_.id);
}

TEST_F(FileFlowTest, SanitizesClientProvidedMimeType) {
  auto session = files_service_->BeginUpload(
      Owner(), UploadRequest{root_id_, "weird.bin", "text/plain\r\nX-Injected: 1"});
  ASSERT_TRUE(session.Ok());
  const auto upload = session.Value();
  ASSERT_TRUE(upload->Write("x", 1).Ok());
  const auto created = upload->Finish();
  ASSERT_TRUE(created.Ok());
  EXPECT_EQ(created.Value().mime_type, "application/octet-stream")
      << "非法 MIME 必须回落到默认值，避免响应头注入";
}

}  // namespace
}  // namespace lan_file_hub
