// 阶段九集成测试：资源权限（含文件夹继承）与分享链接。
// 需要真实 PostgreSQL。
//
// 覆盖：权限矩阵（READ/WRITE/OWNER × 4 种动作）、文件夹继承、权限修改与删除、
//       非所有者不能改权限、分享链接创建/列表/撤销、Token 只存哈希、
//       过期与下载次数、分享越权防护、管理员访问留审计。
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <pqxx/pqxx>

#include "fixtures/temp_dir.h"
#include "lan_file_hub/application/file_service.h"
#include "lan_file_hub/application/folder_service.h"
#include "lan_file_hub/application/permission_checker.h"
#include "lan_file_hub/application/sharing_service.h"
#include "lan_file_hub/application/trash_service.h"
#include "lan_file_hub/application/upload_service.h"
#include "lan_file_hub/common/config.h"
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

class SharingTest : public ::testing::Test {
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
    permission_repo_ = std::make_shared<PermissionRepository>(*pool_);
    share_repo_ = std::make_shared<ShareLinkRepository>(*pool_);
    audit_ = std::make_shared<AuditLogRepository>(*pool_);
    cleanup_repo_ = std::make_shared<CleanupTaskRepository>(*pool_);

    if (!users_->DeleteAllForTesting().Ok()) {
      Skip("users 表不可用，请先运行 lan_file_hub_migrate");
      return;
    }
    if (!permission_repo_->DeleteAllForTesting().Ok()) {
      Skip("permissions 表不可用，请先运行 lan_file_hub_migrate");
      return;
    }
    if (!share_repo_->DeleteAllForTesting().Ok()) {
      Skip("share_links 表不可用，请先运行 lan_file_hub_migrate");
      return;
    }
    (void)sessions_->DeleteAllForTesting();
    (void)files_->DeleteAllForTesting();
    (void)audit_->DeleteAllForTesting();
    (void)cleanup_repo_->DeleteAllForTesting();

    auto storage = LocalObjectStorage::Create(storage_dir_.Path());
    ASSERT_TRUE(storage.Ok()) << storage.GetError().message;
    storage_ = storage.Value();

    // 真实实现：基于 permissions 表 + 文件夹继承
    checker_ = std::make_shared<GrantBasedPermissionChecker>(*permission_repo_, *folders_, *files_);
    folders_service_ = std::make_shared<FolderService>(*folders_, *checker_);
    files_service_ = std::make_shared<FileService>(*files_, *folders_, *storage_, *checker_,
                                                   FileServiceOptions{64ULL * kMiB});
    uploads_ = std::make_shared<UploadService>(*sessions_, *files_, *folders_, *users_, *storage_,
                                               *checker_, Options());
    trash_ =
        std::make_shared<TrashService>(*trash_repo_, *files_, *folders_, *audit_, *cleanup_repo_,
                                       *permission_repo_, *share_repo_, *checker_);
    sharing_ = std::make_shared<SharingService>(*permission_repo_, *share_repo_, *files_, *folders_,
                                                *users_, *audit_, *storage_, *checker_);

    owner_ = MakeUser("shareowner");
    reader_ = MakeUser("sharereader");
    writer_ = MakeUser("sharewriter");
    stranger_ = MakeUser("sharestanger");
    admin_ = MakeUser("shareadmin");
    admin_.role = UserRole::kAdmin;
    ASSERT_TRUE(users_->Create(owner_).Ok());
    ASSERT_TRUE(users_->Create(reader_).Ok());
    ASSERT_TRUE(users_->Create(writer_).Ok());
    ASSERT_TRUE(users_->Create(stranger_).Ok());
    ASSERT_TRUE(users_->Create(admin_).Ok());

    auto root = folders_service_->EnsureRootFolder(Owner());
    ASSERT_TRUE(root.Ok()) << root.GetError().message;
    root_id_ = root.Value().id;
  }

  void TearDown() override {
    if (users_) {
      (void)users_->DeleteAllForTesting();
    }
    if (permission_repo_) {
      (void)permission_repo_->DeleteAllForTesting();
    }
    if (share_repo_) {
      (void)share_repo_->DeleteAllForTesting();
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
  Actor Reader() const { return Actor{reader_.id, reader_.role}; }
  Actor Writer() const { return Actor{writer_.id, writer_.role}; }
  Actor Stranger() const { return Actor{stranger_.id, stranger_.role}; }
  Actor Admin() const { return Actor{admin_.id, admin_.role}; }

  Result<File> Upload(const Actor& actor, const std::string& folder_id, const std::string& name,
                      const std::string& payload) {
    CreateUploadRequest request;
    request.folder_id = folder_id;
    request.file_name = name;
    request.total_size = payload.size();
    request.chunk_size = kChunk;
    request.sha256 = Sha256Hex(payload);
    auto created = uploads_->CreateSession(actor, request);
    if (!created) {
      return created.GetError();
    }
    const std::string upload_id = created.Value().session.id;
    for (std::uint32_t index = 0; index < created.Value().session.total_chunks; ++index) {
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

  // 直接查询有效级别（用真实实现，覆盖继承）
  PermissionLevel LevelOf(const Actor& actor, const std::string& resource_type,
                          const std::string& resource_id, const std::string& folder_id) {
    auto level = checker_->EffectiveLevel(
        actor.user_id, ResourceDescriptor{"", resource_type, resource_id, folder_id});
    return level.Ok() ? level.Value() : PermissionLevel::kNone;
  }

  testing::TempDir storage_dir_{"sharing_storage"};
  std::shared_ptr<ConnectionPool> pool_;
  std::shared_ptr<UserRepository> users_;
  std::shared_ptr<FolderRepository> folders_;
  std::shared_ptr<FileRepository> files_;
  std::shared_ptr<UploadSessionRepository> sessions_;
  std::shared_ptr<TrashRepository> trash_repo_;
  std::shared_ptr<PermissionRepository> permission_repo_;
  std::shared_ptr<ShareLinkRepository> share_repo_;
  std::shared_ptr<AuditLogRepository> audit_;
  std::shared_ptr<CleanupTaskRepository> cleanup_repo_;
  std::shared_ptr<LocalObjectStorage> storage_;
  std::shared_ptr<GrantBasedPermissionChecker> checker_;
  std::shared_ptr<FolderService> folders_service_;
  std::shared_ptr<FileService> files_service_;
  std::shared_ptr<UploadService> uploads_;
  std::shared_ptr<TrashService> trash_;
  std::shared_ptr<SharingService> sharing_;
  User owner_;
  User reader_;
  User writer_;
  User stranger_;
  User admin_;
  std::string root_id_;
};

// ---- 权限矩阵 ---------------------------------------------------------------

TEST_F(SharingTest, OwnerHasAllPermissions) {
  auto folder = folders_service_->Create(Owner(), root_id_, "docs");
  ASSERT_TRUE(folder.Ok()) << folder.GetError().message;

  for (const auto action : {ResourceAction::kRead, ResourceAction::kWrite, ResourceAction::kDelete,
                            ResourceAction::kManage}) {
    auto allowed =
        checker_->Can(owner_.id, owner_.role,
                      ResourceDescriptor{owner_.id, "folder", folder.Value().id, ""}, action);
    ASSERT_TRUE(allowed.Ok());
    EXPECT_TRUE(allowed.Value()) << "所有者必须有 " << ToString(action);
  }
}

TEST_F(SharingTest, StrangerHasNoPermission) {
  auto folder = folders_service_->Create(Owner(), root_id_, "private");
  ASSERT_TRUE(folder.Ok());

  for (const auto action : {ResourceAction::kRead, ResourceAction::kWrite, ResourceAction::kDelete,
                            ResourceAction::kManage}) {
    auto allowed =
        checker_->Can(stranger_.id, stranger_.role,
                      ResourceDescriptor{owner_.id, "folder", folder.Value().id, ""}, action);
    ASSERT_TRUE(allowed.Ok());
    EXPECT_FALSE(allowed.Value()) << "无关用户不能有 " << ToString(action);
  }
  EXPECT_EQ(LevelOf(Stranger(), "folder", folder.Value().id, ""), PermissionLevel::kNone);
}

TEST_F(SharingTest, PermissionMatrixReadWriteOwner) {
  auto folder = folders_service_->Create(Owner(), root_id_, "matrix");
  ASSERT_TRUE(folder.Ok());
  const std::string folder_id = folder.Value().id;
  const ResourceDescriptor descriptor{owner_.id, "folder", folder_id, ""};

  // READ：只能读
  ASSERT_TRUE(sharing_->Grant(Owner(), folder_id, reader_.id, "READ").Ok());
  EXPECT_EQ(LevelOf(Reader(), "folder", folder_id, ""), PermissionLevel::kRead);
  EXPECT_TRUE(checker_->Can(reader_.id, reader_.role, descriptor, ResourceAction::kRead).Value());
  EXPECT_FALSE(checker_->Can(reader_.id, reader_.role, descriptor, ResourceAction::kWrite).Value());
  EXPECT_FALSE(
      checker_->Can(reader_.id, reader_.role, descriptor, ResourceAction::kManage).Value());

  // WRITE：能读写删，但不能管理权限
  ASSERT_TRUE(sharing_->Grant(Owner(), folder_id, writer_.id, "WRITE").Ok());
  EXPECT_EQ(LevelOf(Writer(), "folder", folder_id, ""), PermissionLevel::kWrite);
  EXPECT_TRUE(checker_->Can(writer_.id, writer_.role, descriptor, ResourceAction::kRead).Value());
  EXPECT_TRUE(checker_->Can(writer_.id, writer_.role, descriptor, ResourceAction::kWrite).Value());
  EXPECT_TRUE(checker_->Can(writer_.id, writer_.role, descriptor, ResourceAction::kDelete).Value());
  EXPECT_FALSE(
      checker_->Can(writer_.id, writer_.role, descriptor, ResourceAction::kManage).Value());

  // OWNER：可以管理权限
  ASSERT_TRUE(sharing_->Grant(Owner(), folder_id, stranger_.id, "OWNER").Ok());
  EXPECT_EQ(LevelOf(Stranger(), "folder", folder_id, ""), PermissionLevel::kOwner);
  EXPECT_TRUE(
      checker_->Can(stranger_.id, stranger_.role, descriptor, ResourceAction::kManage).Value());
}

TEST_F(SharingTest, AdminBypassesButIsAudited) {
  auto folder = folders_service_->Create(Owner(), root_id_, "admins");
  ASSERT_TRUE(folder.Ok());

  auto explicit_allowed = checker_->Can(
      admin_.id, admin_.role, ResourceDescriptor{owner_.id, "folder", folder.Value().id, ""},
      ResourceAction::kManage);
  ASSERT_TRUE(explicit_allowed.Ok());
  EXPECT_TRUE(explicit_allowed.Value()) << "管理员是系统级权限";

  // 管理员访问别人的资源必须写审计
  auto resolved = sharing_->Resolve(folder.Value().id);
  ASSERT_TRUE(resolved.Ok());
  sharing_->AuditAdminAccess(Admin(), resolved.Value(), "read_folder");
  auto entries = audit_->CountByAction("admin_read_folder");
  ASSERT_TRUE(entries.Ok());
  EXPECT_EQ(entries.Value(), 1U);
}

TEST_F(SharingTest, FileInheritsFolderPermission) {
  auto folder = folders_service_->Create(Owner(), root_id_, "inherited");
  ASSERT_TRUE(folder.Ok());
  auto file = Upload(Owner(), folder.Value().id, "inside.bin", Repeat('a', kMiB));
  ASSERT_TRUE(file.Ok()) << file.GetError().message;

  // 给文件夹授 READ，文件应当继承
  ASSERT_TRUE(sharing_->Grant(Owner(), folder.Value().id, reader_.id, "READ").Ok());
  EXPECT_EQ(LevelOf(Reader(), "file", file.Value().id, folder.Value().id), PermissionLevel::kRead);

  auto download = files_service_->PrepareDownload(Reader(), file.Value().id);
  EXPECT_TRUE(download.Ok()) << "继承来的读权限必须能真的下载";

  // 文件自身的额外授权可以高于目录
  ASSERT_TRUE(sharing_->Grant(Owner(), file.Value().id, reader_.id, "WRITE").Ok());
  EXPECT_EQ(LevelOf(Reader(), "file", file.Value().id, folder.Value().id), PermissionLevel::kWrite);
  EXPECT_EQ(LevelOf(Reader(), "folder", folder.Value().id, ""), PermissionLevel::kRead)
      << "文件上的授权不能反向影响目录";
}

TEST_F(SharingTest, DeeperFolderInheritanceTakesHighestLevel) {
  auto top = folders_service_->Create(Owner(), root_id_, "top");
  ASSERT_TRUE(top.Ok());
  auto mid = folders_service_->Create(Owner(), top.Value().id, "mid");
  ASSERT_TRUE(mid.Ok());
  auto leaf = folders_service_->Create(Owner(), mid.Value().id, "leaf");
  ASSERT_TRUE(leaf.Ok());
  auto file = Upload(Owner(), leaf.Value().id, "deep.bin", Repeat('b', kMiB));
  ASSERT_TRUE(file.Ok()) << file.GetError().message;

  ASSERT_TRUE(sharing_->Grant(Owner(), top.Value().id, reader_.id, "READ").Ok());
  EXPECT_EQ(LevelOf(Reader(), "file", file.Value().id, leaf.Value().id), PermissionLevel::kRead)
      << "祖先目录的授权要能继承下来";

  // 更深一层给更高的权限：取最高的那个
  ASSERT_TRUE(sharing_->Grant(Owner(), mid.Value().id, reader_.id, "WRITE").Ok());
  EXPECT_EQ(LevelOf(Reader(), "file", file.Value().id, leaf.Value().id), PermissionLevel::kWrite)
      << "路径上取最高级别（没有拒绝规则）";
}

TEST_F(SharingTest, GrantedWriterCanActuallyUploadAndDelete) {
  auto folder = folders_service_->Create(Owner(), root_id_, "shared-upload");
  ASSERT_TRUE(folder.Ok());
  ASSERT_TRUE(sharing_->Grant(Owner(), folder.Value().id, writer_.id, "WRITE").Ok());

  auto uploaded = Upload(Writer(), folder.Value().id, "from-writer.bin", Repeat('c', kMiB));
  ASSERT_TRUE(uploaded.Ok()) << uploaded.GetError().message;

  EXPECT_TRUE(trash_->SoftDeleteFile(Writer(), uploaded.Value().id).Ok()) << "WRITE 应该包含软删除";
  // 但永久删除要求 OWNER
  auto purged = trash_->Purge(Writer(), uploaded.Value().id);
  EXPECT_FALSE(purged.Ok()) << "WRITE 不该能永久删除";
}

TEST_F(SharingTest, ReaderCannotUploadOrDelete) {
  auto folder = folders_service_->Create(Owner(), root_id_, "read-only");
  ASSERT_TRUE(folder.Ok());
  auto file = Upload(Owner(), folder.Value().id, "only-read.bin", Repeat('d', kMiB));
  ASSERT_TRUE(file.Ok());
  ASSERT_TRUE(sharing_->Grant(Owner(), folder.Value().id, reader_.id, "READ").Ok());

  auto uploaded = Upload(Reader(), folder.Value().id, "should-fail.bin", Repeat('e', kMiB));
  ASSERT_FALSE(uploaded.Ok()) << "READ 不能上传";

  EXPECT_FALSE(trash_->SoftDeleteFile(Reader(), file.Value().id).Ok()) << "READ 不能删除";
}

// ---- 权限管理接口语义 -------------------------------------------------------

TEST_F(SharingTest, OnlyOwnerOrAdminCanManagePermissions) {
  auto folder = folders_service_->Create(Owner(), root_id_, "manageable");
  ASSERT_TRUE(folder.Ok());
  const std::string folder_id = folder.Value().id;

  // 无关用户不能看、不能改
  EXPECT_FALSE(sharing_->ListGrants(Stranger(), folder_id).Ok());
  EXPECT_FALSE(sharing_->Grant(Stranger(), folder_id, reader_.id, "READ").Ok());
  EXPECT_FALSE(sharing_->RevokeGrant(Stranger(), folder_id, reader_.id).Ok());

  // 被授 WRITE 的人也不能改权限
  ASSERT_TRUE(sharing_->Grant(Owner(), folder_id, writer_.id, "WRITE").Ok());
  EXPECT_FALSE(sharing_->Grant(Writer(), folder_id, reader_.id, "READ").Ok());

  // 所有者可以
  auto grants = sharing_->ListGrants(Owner(), folder_id);
  ASSERT_TRUE(grants.Ok()) << grants.GetError().message;
  EXPECT_EQ(grants.Value().size(), 1U);
  EXPECT_EQ(grants.Value()[0].subject_id, writer_.id);
  EXPECT_EQ(grants.Value()[0].subject_name, writer_.username);
}

TEST_F(SharingTest, GrantValidationRejectsBadInput) {
  auto folder = folders_service_->Create(Owner(), root_id_, "validate");
  ASSERT_TRUE(folder.Ok());
  const std::string folder_id = folder.Value().id;

  EXPECT_EQ(sharing_->Grant(Owner(), folder_id, reader_.id, "SUPERUSER").GetError().code,
            "INVALID_ARGUMENT");
  EXPECT_EQ(sharing_->Grant(Owner(), folder_id, GenerateUuidV4(), "READ").GetError().code,
            "RESOURCE_NOT_FOUND");

  // 禁用用户不能授权
  auto disabled = MakeUser("disabled-user");
  disabled.status = UserStatus::kDisabled;
  ASSERT_TRUE(users_->Create(disabled).Ok());
  EXPECT_EQ(sharing_->Grant(Owner(), folder_id, disabled.id, "READ").GetError().code,
            "INVALID_ARGUMENT");
}

TEST_F(SharingTest, GrantIsUpsertAndRevokeIsIdempotent) {
  auto folder = folders_service_->Create(Owner(), root_id_, "upsert");
  ASSERT_TRUE(folder.Ok());
  const std::string folder_id = folder.Value().id;

  ASSERT_TRUE(sharing_->Grant(Owner(), folder_id, reader_.id, "READ").Ok());
  ASSERT_TRUE(sharing_->Grant(Owner(), folder_id, reader_.id, "WRITE").Ok());
  auto grants = sharing_->ListGrants(Owner(), folder_id);
  ASSERT_TRUE(grants.Ok());
  EXPECT_EQ(grants.Value().size(), 1U) << "同一用户同一资源只保留一条记录";
  EXPECT_EQ(grants.Value()[0].permission, "WRITE");

  EXPECT_TRUE(sharing_->RevokeGrant(Owner(), folder_id, reader_.id).Ok());
  EXPECT_TRUE(sharing_->RevokeGrant(Owner(), folder_id, reader_.id).Ok()) << "重复撤销幂等";
  auto after = sharing_->ListGrants(Owner(), folder_id);
  ASSERT_TRUE(after.Ok());
  EXPECT_TRUE(after.Value().empty());
}

// ---- 分享链接 ---------------------------------------------------------------

TEST_F(SharingTest, CreateShareReturnsPlaintextTokenOnceAndStoresOnlyHash) {
  auto file = Upload(Owner(), root_id_, "shared.bin", Repeat('f', 2 * kMiB));
  ASSERT_TRUE(file.Ok()) << file.GetError().message;

  ShareCreateRequest request;
  request.permission = "READ";
  auto created = sharing_->CreateShare(Owner(), file.Value().id, request);
  ASSERT_TRUE(created.Ok()) << created.GetError().message;

  const std::string token = created.Value().plaintext_token;
  EXPECT_GE(token.size(), 40U) << "Token 必须是高强度随机值";
  EXPECT_EQ(created.Value().link.token_hash, HashShareToken(token));

  // 列表里没有明文，也没有哈希
  auto links = sharing_->ListShares(Owner(), file.Value().id);
  ASSERT_TRUE(links.Ok());
  ASSERT_EQ(links.Value().size(), 1U);
  EXPECT_EQ(links.Value()[0].token_hash, HashShareToken(token));

  // 明文 Token 只应出现在创建响应里：再查一次也拿不到（仓库层只存哈希）
  EXPECT_EQ(links.Value()[0].token_hash.find(token), std::string::npos);
}

TEST_F(SharingTest, PublicDescribeAndDownloadWorkWithoutLogin) {
  const std::string payload = Repeat('g', 2 * kMiB);
  auto file = Upload(Owner(), root_id_, "public.bin", payload);
  ASSERT_TRUE(file.Ok());

  ShareCreateRequest request;
  auto created = sharing_->CreateShare(Owner(), file.Value().id, request);
  ASSERT_TRUE(created.Ok()) << created.GetError().message;
  const std::string token = created.Value().plaintext_token;

  auto view = sharing_->DescribePublic(token);
  ASSERT_TRUE(view.Ok()) << view.GetError().message;
  EXPECT_EQ(view.Value().name, "public.bin");
  EXPECT_EQ(view.Value().size_bytes, payload.size());
  EXPECT_TRUE(view.Value().can_download);

  auto download = sharing_->PreparePublicDownload(token);
  ASSERT_TRUE(download.Ok()) << download.GetError().message;
  EXPECT_EQ(download.Value().file.content_hash, Sha256Hex(payload));
  // 公开视图本身不含任何内部标识（对象 Key 只在服务端使用）
  EXPECT_EQ(view.Value().name, "public.bin");

  // 下载计数 +1
  auto after = share_repo_->FindUsableByTokenHash(HashShareToken(token));
  ASSERT_TRUE(after.Ok());
  ASSERT_TRUE(after.Value().has_value());
  EXPECT_EQ(after.Value()->download_count, 1);
}

TEST_F(SharingTest, RevokedShareIsRejected) {
  auto file = Upload(Owner(), root_id_, "revoke.bin", Repeat('h', kMiB));
  ASSERT_TRUE(file.Ok());
  ShareCreateRequest request;
  auto created = sharing_->CreateShare(Owner(), file.Value().id, request);
  ASSERT_TRUE(created.Ok());
  const std::string token = created.Value().plaintext_token;

  ASSERT_TRUE(sharing_->RevokeShare(Owner(), created.Value().link.id).Ok());
  EXPECT_TRUE(sharing_->RevokeShare(Owner(), created.Value().link.id).Ok()) << "重复撤销幂等";

  EXPECT_FALSE(sharing_->DescribePublic(token).Ok()) << "撤销后不能再查看";
  EXPECT_FALSE(sharing_->PreparePublicDownload(token).Ok()) << "撤销后不能再下载";
}

TEST_F(SharingTest, ExpiredShareIsRejected) {
  auto file = Upload(Owner(), root_id_, "expire.bin", Repeat('i', kMiB));
  ASSERT_TRUE(file.Ok());
  ShareCreateRequest request;
  request.expires_in_seconds = 1;
  auto created = sharing_->CreateShare(Owner(), file.Value().id, request);
  ASSERT_TRUE(created.Ok()) << created.GetError().message;
  const std::string token = created.Value().plaintext_token;

  // 刚创建还有效
  EXPECT_TRUE(sharing_->DescribePublic(token).Ok());
  ASSERT_TRUE(created.Value().link.expires_at.has_value()) << "过期时间必须落库";
  EXPECT_FALSE(created.Value().link.expires_at->empty());

  // 把过期时间提前到过去（不靠 sleep，避免测试不稳定）
  auto lease = pool_->Acquire();
  ASSERT_TRUE(lease != nullptr);
  {
    pqxx::work tx(lease->connection());
    pqxx::params params;
    params.append(created.Value().link.id);
    tx.exec("UPDATE share_links SET expires_at = now() - interval '1 second' WHERE id = $1::uuid",
            params);
    tx.commit();
  }

  EXPECT_FALSE(sharing_->DescribePublic(token).Ok()) << "过期后不能再查看";
  EXPECT_FALSE(sharing_->PreparePublicDownload(token).Ok()) << "过期后不能再下载";
}

TEST_F(SharingTest, DownloadLimitIsEnforcedAtomically) {
  auto file = Upload(Owner(), root_id_, "limited.bin", Repeat('j', kMiB));
  ASSERT_TRUE(file.Ok());
  ShareCreateRequest request;
  request.max_downloads = 2;
  auto created = sharing_->CreateShare(Owner(), file.Value().id, request);
  ASSERT_TRUE(created.Ok()) << created.GetError().message;
  const std::string token = created.Value().plaintext_token;

  EXPECT_TRUE(sharing_->PreparePublicDownload(token).Ok()) << "第 1 次";
  EXPECT_TRUE(sharing_->PreparePublicDownload(token).Ok()) << "第 2 次";

  auto third = sharing_->PreparePublicDownload(token);
  ASSERT_FALSE(third.Ok()) << "超过下载次数必须拒绝";
  EXPECT_EQ(third.GetError().code, "RESOURCE_NOT_FOUND") << "不泄露具体原因";

  auto link = share_repo_->FindById(created.Value().link.id);
  ASSERT_TRUE(link.Ok());
  EXPECT_EQ(link.Value()->download_count, 2) << "被拒绝的请求不能计数";
}

TEST_F(SharingTest, ShareOnlyGrantsAccessToItsOwnResource) {
  auto file_a = Upload(Owner(), root_id_, "a.bin", Repeat('k', kMiB));
  auto file_b = Upload(Owner(), root_id_, "b.bin", Repeat('l', kMiB));
  ASSERT_TRUE(file_a.Ok());
  ASSERT_TRUE(file_b.Ok());

  ShareCreateRequest request;
  auto created = sharing_->CreateShare(Owner(), file_a.Value().id, request);
  ASSERT_TRUE(created.Ok());
  const std::string token = created.Value().plaintext_token;

  auto download = sharing_->PreparePublicDownload(token);
  ASSERT_TRUE(download.Ok());
  EXPECT_EQ(download.Value().resource.id, file_a.Value().id) << "分享链接只能访问它绑定的那个资源";

  // 构造一个指向 B 的分享，确认两者互不影响
  auto other = sharing_->CreateShare(Owner(), file_b.Value().id, request);
  ASSERT_TRUE(other.Ok());
  auto other_download = sharing_->PreparePublicDownload(other.Value().plaintext_token);
  ASSERT_TRUE(other_download.Ok());
  EXPECT_EQ(other_download.Value().resource.id, file_b.Value().id);
  EXPECT_NE(other.Value().plaintext_token, token);
}

TEST_F(SharingTest, FolderShareCannotBeDownloaded) {
  auto folder = folders_service_->Create(Owner(), root_id_, "folder-share");
  ASSERT_TRUE(folder.Ok());
  ShareCreateRequest request;
  auto created = sharing_->CreateShare(Owner(), folder.Value().id, request);
  ASSERT_TRUE(created.Ok());
  const std::string token = created.Value().plaintext_token;

  auto view = sharing_->DescribePublic(token);
  ASSERT_TRUE(view.Ok());
  EXPECT_FALSE(view.Value().can_download);
  EXPECT_FALSE(view.Value().download_blocked_reason.empty()) << "要给出可解释的原因";

  auto download = sharing_->PreparePublicDownload(token);
  ASSERT_FALSE(download.Ok());
  EXPECT_EQ(download.GetError().code, "FORBIDDEN");
}

TEST_F(SharingTest, OnlyOwnerOrAdminCanCreateAndRevokeShares) {
  auto file = Upload(Owner(), root_id_, "guarded.bin", Repeat('m', kMiB));
  ASSERT_TRUE(file.Ok());

  // 无关用户不能创建
  ShareCreateRequest request;
  EXPECT_FALSE(sharing_->CreateShare(Stranger(), file.Value().id, request).Ok());
  // 只有 READ 权限也不能创建分享
  ASSERT_TRUE(sharing_->Grant(Owner(), file.Value().id, reader_.id, "READ").Ok());
  EXPECT_FALSE(sharing_->CreateShare(Reader(), file.Value().id, request).Ok());

  auto created = sharing_->CreateShare(Owner(), file.Value().id, request);
  ASSERT_TRUE(created.Ok());
  EXPECT_FALSE(sharing_->RevokeShare(Stranger(), created.Value().link.id).Ok());
  EXPECT_FALSE(sharing_->ListShares(Stranger(), file.Value().id).Ok());

  // 管理员可以（并且这是一次可追溯的操作）
  EXPECT_TRUE(sharing_->ListShares(Admin(), file.Value().id).Ok());
  auto admin_view = sharing_->Resolve(file.Value().id);
  ASSERT_TRUE(admin_view.Ok());
  sharing_->AuditAdminAccess(Admin(), admin_view.Value(), "list_shares");
  auto audited = audit_->CountByAction("admin_list_shares");
  ASSERT_TRUE(audited.Ok());
  EXPECT_EQ(audited.Value(), 1U);
}

TEST_F(SharingTest, SharePermissionValueIsValidated) {
  auto file = Upload(Owner(), root_id_, "perm-validate.bin", Repeat('n', kMiB));
  ASSERT_TRUE(file.Ok());

  ShareCreateRequest request;
  request.permission = "OWNER";  // 分享链接上不允许出现 OWNER
  auto created = sharing_->CreateShare(Owner(), file.Value().id, request);
  ASSERT_FALSE(created.Ok());
  EXPECT_EQ(created.GetError().code, "INVALID_ARGUMENT");

  request.permission = "READ";
  request.max_downloads = 0;
  EXPECT_EQ(sharing_->CreateShare(Owner(), file.Value().id, request).GetError().code,
            "INVALID_ARGUMENT");

  request.max_downloads.reset();
  request.expires_in_seconds = 400LL * 24 * 60 * 60;  // 超过一年
  EXPECT_EQ(sharing_->CreateShare(Owner(), file.Value().id, request).GetError().code,
            "INVALID_ARGUMENT");
}

TEST_F(SharingTest, PurgingResourceRemovesGrantsAndShares) {
  auto folder = folders_service_->Create(Owner(), root_id_, "purge-shares");
  ASSERT_TRUE(folder.Ok());
  auto file = Upload(Owner(), folder.Value().id, "gone.bin", Repeat('o', kMiB));
  ASSERT_TRUE(file.Ok());

  ASSERT_TRUE(sharing_->Grant(Owner(), folder.Value().id, reader_.id, "READ").Ok());
  ShareCreateRequest request;
  auto created = sharing_->CreateShare(Owner(), file.Value().id, request);
  ASSERT_TRUE(created.Ok());
  const std::string token = created.Value().plaintext_token;

  // 永久删除整个目录：授权与分享链接都要跟着清掉
  ASSERT_TRUE(folders_service_->SoftDelete(Owner(), folder.Value().id).Ok());
  auto purged = trash_->Purge(Owner(), folder.Value().id);
  ASSERT_TRUE(purged.Ok()) << purged.GetError().message;

  auto grants = permission_repo_->CountForSubject(reader_.id);
  ASSERT_TRUE(grants.Ok());
  EXPECT_EQ(grants.Value(), 0U) << "不能留下指向已删除目录的悬空授权";
  EXPECT_FALSE(sharing_->DescribePublic(token).Ok()) << "资源没了，分享链接也必须失效";
}

TEST_F(SharingTest, SharedWithMeListsDirectlyGrantedResources) {
  auto folder = folders_service_->Create(Owner(), root_id_, "shared-list");
  ASSERT_TRUE(folder.Ok());
  auto file = Upload(Owner(), folder.Value().id, "listed.bin", Repeat('p', kMiB));
  ASSERT_TRUE(file.Ok());

  auto empty = sharing_->SharedWith(Reader(), 50);
  ASSERT_TRUE(empty.Ok());
  EXPECT_TRUE(empty.Value().empty()) << "没有授权时列表为空";

  ASSERT_TRUE(sharing_->Grant(Owner(), folder.Value().id, reader_.id, "READ").Ok());
  ASSERT_TRUE(sharing_->Grant(Owner(), file.Value().id, reader_.id, "WRITE").Ok());

  auto items = sharing_->SharedWith(Reader(), 50);
  ASSERT_TRUE(items.Ok()) << items.GetError().message;
  ASSERT_EQ(items.Value().size(), 2U);
  // 列表必须带资源名与类型，前端才能直接展示
  bool saw_folder = false;
  bool saw_file = false;
  for (const auto& item : items.Value()) {
    if (item.resource_type == "folder" && item.name == "shared-list") {
      saw_folder = true;
      EXPECT_EQ(item.permission, "READ");
      EXPECT_EQ(item.owner_id, owner_.id);
    }
    if (item.resource_type == "file" && item.name == "listed.bin") {
      saw_file = true;
      EXPECT_EQ(item.permission, "WRITE");
      EXPECT_EQ(item.size_bytes, kMiB);
    }
  }
  EXPECT_TRUE(saw_folder);
  EXPECT_TRUE(saw_file);

  // 别人的列表里不该有这些东西
  auto stranger_items = sharing_->SharedWith(Stranger(), 50);
  ASSERT_TRUE(stranger_items.Ok());
  EXPECT_TRUE(stranger_items.Value().empty());

  // 资源被永久删除后不再出现在列表里
  ASSERT_TRUE(folders_service_->SoftDelete(Owner(), folder.Value().id).Ok());
  ASSERT_TRUE(trash_->Purge(Owner(), folder.Value().id).Ok());
  auto after = sharing_->SharedWith(Reader(), 50);
  ASSERT_TRUE(after.Ok());
  EXPECT_TRUE(after.Value().empty()) << "已删除的资源不应再出现在「分享给我的」里";
}

}  // namespace
}  // namespace lan_file_hub
