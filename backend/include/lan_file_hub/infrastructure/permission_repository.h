// 资源权限与分享链接仓储（阶段九）。
//
// 权限继承靠文件夹的 materialized path（`folders.path_key`）实现：
// 目标目录的 path_key 是 `/a/b/c/`，那么所有 `path_key` 是它前缀的文件夹都是它的祖先，
// 一条 LIKE 查询就能取出整条路径上的授权。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lan_file_hub/common/result.h"
#include "lan_file_hub/domain/user.h"
#include "lan_file_hub/infrastructure/database.h"

namespace lan_file_hub {

struct PermissionGrant {
  std::string id;
  std::string resource_type;  // file | folder
  std::string resource_id;
  std::string subject_type;  // USER
  std::string subject_id;
  std::string subject_name;  // 便于权限管理页展示（用户被删后为空）
  std::string permission;    // READ | WRITE | OWNER
  std::string created_at;
  std::string updated_at;
};

class PermissionRepository {
 public:
  explicit PermissionRepository(ConnectionPool& pool) : pool_(pool) {}

  // 授予或更新（同一个用户对同一个资源只有一条记录）
  [[nodiscard]] Result<PermissionGrant> Upsert(std::string_view resource_type,
                                               std::string_view resource_id,
                                               std::string_view subject_id,
                                               std::string_view permission,
                                               std::string_view created_by);

  [[nodiscard]] Status Remove(std::string_view resource_type, std::string_view resource_id,
                              std::string_view subject_id);

  [[nodiscard]] Result<std::vector<PermissionGrant>> ListForResource(std::string_view resource_type,
                                                                     std::string_view resource_id);

  [[nodiscard]] Result<std::optional<std::string>> FindDirect(std::string_view resource_type,
                                                              std::string_view resource_id,
                                                              std::string_view subject_id);

  // 整条路径上的授权（资源自身 + 祖先文件夹），用于计算有效级别
  [[nodiscard]] Result<std::vector<std::string>> ListLevelsOnPath(std::string_view subject_id,
                                                                  std::string_view path_key);

  // 永久删除资源时清理悬空授权（多态引用没法用外键，必须显式删）
  [[nodiscard]] Status DeleteForResource(std::string_view resource_type,
                                         std::string_view resource_id);
  [[nodiscard]] Status DeleteForFolderSubtree(std::string_view path_key_prefix);

  [[nodiscard]] Result<std::uint64_t> CountForSubject(std::string_view subject_id);

  // 「分享给我的」：该用户被**直接授权**的资源（含资源名与类型），供前端列表使用。
  // 已删除的资源不出现；继承来的权限也不列在这里（列表要可操作、可解释）。
  struct SharedResource {
    std::string resource_type;
    std::string resource_id;
    std::string name;
    std::uint64_t size_bytes{0};
    std::string permission;
    std::string owner_id;
    std::string granted_at;
  };
  [[nodiscard]] Result<std::vector<SharedResource>> ListSharedWith(std::string_view subject_id,
                                                                   int limit);

  [[nodiscard]] Status DeleteAllForTesting();

 private:
  ConnectionPool& pool_;
};

struct ShareLink {
  std::string id;
  std::string token_hash;
  std::string resource_type;
  std::string resource_id;
  std::string permission;  // READ | WRITE
  // 创建时二选一：相对有效期（由数据库按 now() 计算，避免应用时钟偏差）或绝对时间
  std::optional<std::int64_t> expires_in_seconds;
  std::optional<std::string> expires_at;
  std::optional<std::int32_t> max_downloads;
  std::int32_t download_count{0};
  std::optional<std::string> revoked_at;
  std::string created_by;
  std::optional<std::string> last_access_at;
  std::string created_at;
  // 便于列表展示（创建时填充）
  std::string resource_name;
};

class ShareLinkRepository {
 public:
  explicit ShareLinkRepository(ConnectionPool& pool) : pool_(pool) {}

  [[nodiscard]] Result<ShareLink> Create(const ShareLink& link);

  // 公开访问：按 Token 哈希查；**只返回仍然有效的记录**
  [[nodiscard]] Result<std::optional<ShareLink>> FindUsableByTokenHash(std::string_view token_hash);

  // 管理用：不限状态，便于展示「已过期/已撤销」
  [[nodiscard]] Result<std::vector<ShareLink>> ListForResource(std::string_view resource_type,
                                                               std::string_view resource_id);

  [[nodiscard]] Result<std::optional<ShareLink>> FindById(std::string_view id);

  // 撤销（幂等）：已经撤销的返回 false 表示无需改动
  [[nodiscard]] Result<bool> Revoke(std::string_view id, std::string_view actor_id);

  // 下载计数：达到上限时返回 false（原子 UPDATE，避免并发绕过）
  [[nodiscard]] Result<bool> ConsumeDownload(std::string_view id);

  [[nodiscard]] Result<std::uint64_t> CountByResource(std::string_view resource_type,
                                                      std::string_view resource_id);

  [[nodiscard]] Status DeleteForResource(std::string_view resource_type,
                                         std::string_view resource_id);
  [[nodiscard]] Status DeleteForFolderSubtree(std::string_view path_key_prefix);

  [[nodiscard]] Status DeleteAllForTesting();

 private:
  ConnectionPool& pool_;
};

// 生成高强度随机分享 Token（32 字节 → base64url），返回明文；
// 服务端只保存它的 SHA-256，明文只在创建响应里出现一次。
[[nodiscard]] Result<std::string> GenerateShareToken();
[[nodiscard]] std::string HashShareToken(std::string_view token);

}  // namespace lan_file_hub
