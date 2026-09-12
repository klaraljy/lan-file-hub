// 资源权限检查接口。
//
// 阶段三只需要「所有者 + 管理员」这一层，但接口先定下来：
// 阶段九会引入 permissions 表与文件夹继承，届时只要替换实现，调用方不用改。
//
// 约定：**所有权限判断都在服务端**，前端隐藏按钮不算权限控制（docs/security.md）。
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "lan_file_hub/common/result.h"
#include "lan_file_hub/domain/user.h"

namespace lan_file_hub {

class PermissionRepository;
class FolderRepository;
class FileRepository;

enum class ResourceAction {
  kRead,    // 查看目录、读取元数据、下载、查看/下载历史版本
  kWrite,   // 上传、更新、移动、重命名、删除（进入回收站）
  kDelete,  // 同 kWrite，保留语义区分：软删除
  kManage,  // 永久删除、管理权限与分享链接（需要 OWNER）
};

[[nodiscard]] const char* ToString(ResourceAction action);

struct ResourceDescriptor {
  std::string_view owner_id;
  // 阶段九：为了解析文件夹继承，需要知道资源是谁、在哪个目录下。
  // 只填 owner_id 时退化成「所有者 + 管理员」判定（旧调用点仍然可用）。
  std::string_view resource_type;  // "file" | "folder" | 空
  std::string_view resource_id;
  std::string_view folder_id;  // 文件所在目录；文件夹资源可留空
};

// 用户在某资源上的**有效权限级别**（已算上文件夹继承）。
enum class PermissionLevel { kNone = 0, kRead = 1, kWrite = 2, kOwner = 3 };

[[nodiscard]] std::string ToDbString(PermissionLevel level);
[[nodiscard]] PermissionLevel ParsePermissionLevel(const std::string& value);
// action 需要的最低级别：读=READ，写/删除=WRITE，管理=OWNER
[[nodiscard]] PermissionLevel RequiredLevel(ResourceAction action);
[[nodiscard]] bool Satisfies(PermissionLevel level, ResourceAction action);

class ResourcePermissionChecker {
 public:
  virtual ~ResourcePermissionChecker() = default;

  // 判断 actor 能否对资源执行 action。
  // 返回 false 表示拒绝；返回 Error 表示判断本身失败（例如依赖不可用）。
  [[nodiscard]] virtual Result<bool> Can(std::string_view actor_id, UserRole actor_role,
                                         const ResourceDescriptor& resource,
                                         ResourceAction action) const = 0;

  // 有效权限级别（已算上文件夹继承）。基类默认只认「所有者 = OWNER」，
  // 供不依赖授权表的降级实现使用；前端用它决定展示哪些按钮（后端仍然独立校验）。
  [[nodiscard]] virtual Result<PermissionLevel> EffectiveLevel(
      std::string_view actor_id, const ResourceDescriptor& resource) const;
};

// 阶段三实现：资源所有者拥有一切权限；管理员在记录审计的前提下也允许。
// 仍然保留：它是不依赖数据库的降级实现，也被不关心授权表的测试使用。
class OwnerBasedPermissionChecker final : public ResourcePermissionChecker {
 public:
  [[nodiscard]] Result<bool> Can(std::string_view actor_id, UserRole actor_role,
                                 const ResourceDescriptor& resource,
                                 ResourceAction action) const override;
};

// 阶段九实现：基于 permissions 表 + 文件夹继承。
//
// 判定顺序（docs/permissions.md §1、§3）：
//   1. 未登录 → 拒绝
//   2. ADMIN 角色 → 允许（调用方负责写审计：管理员访问用户资源必须留痕）
//   3. 资源所有者（folders.owner_id / files.owner_id）→ OWNER
//   4. 资源自身的授权记录
//   5. 祖先文件夹的授权记录（文件从所属目录开始向上找），取**最高**一级
//   6. 都没有 → 拒绝
class GrantBasedPermissionChecker final : public ResourcePermissionChecker {
 public:
  GrantBasedPermissionChecker(PermissionRepository& permissions, FolderRepository& folders,
                              FileRepository& files);

  [[nodiscard]] Result<bool> Can(std::string_view actor_id, UserRole actor_role,
                                 const ResourceDescriptor& resource,
                                 ResourceAction action) const override;

  // 供权限接口与前端使用：算出有效级别（kNone 表示没有任何权限）
  [[nodiscard]] Result<PermissionLevel> EffectiveLevel(
      std::string_view actor_id, const ResourceDescriptor& resource) const override;

 private:
  // 目标资源所在目录的 materialized path（文件夹资源用自身，文件资源用所属目录）
  [[nodiscard]] Result<std::string> PathKeyFor(const ResourceDescriptor& resource) const;

  PermissionRepository& permissions_;
  FolderRepository& folders_;
  FileRepository& files_;
};

}  // namespace lan_file_hub
