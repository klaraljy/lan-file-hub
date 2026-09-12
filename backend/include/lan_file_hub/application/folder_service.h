// 文件夹应用服务。业务规则集中在这里，Controller 只做参数解析与响应组装。
//
// 阶段三规则（docs/phases/03-folders.md）：
//   - 名称校验：不含 /、\、控制字符，不能是 . 或 ..
//   - 同一父目录下不允许同名
//   - 根目录不可重命名、不可移动、不可删除
//   - 不允许移动到自身或其子目录
//   - 软删除的资源不出现在普通列表中
//   - 删除与移动必须记录操作者
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "lan_file_hub/application/permission_checker.h"
#include "lan_file_hub/common/pagination.h"
#include "lan_file_hub/common/result.h"
#include "lan_file_hub/domain/folder.h"
#include "lan_file_hub/domain/user.h"
#include "lan_file_hub/infrastructure/folder_repository.h"

namespace lan_file_hub {

// 一次操作的执行者身份（来自认证过滤器注入的请求属性）
struct Actor {
  std::string user_id;
  UserRole role{UserRole::kUser};
};

struct FolderListing {
  std::vector<Folder> folders;
  std::uint64_t total{0};
  int page{1};
  int page_size{kDefaultPageSize};
};

class FolderService {
 public:
  FolderService(FolderRepository& folders, const ResourcePermissionChecker& permissions);

  // 取（必要时创建）用户根目录
  [[nodiscard]] Result<Folder> EnsureRootFolder(const Actor& actor);

  [[nodiscard]] Result<Folder> Create(const Actor& actor, std::string_view parent_id,
                                      std::string_view name);

  [[nodiscard]] Result<FolderListing> ListChildren(const Actor& actor, std::string_view folder_id,
                                                   const Pagination& pagination);

  [[nodiscard]] Result<Folder> Get(const Actor& actor, std::string_view folder_id);

  // 从根目录到该目录的完整链路（含自身），供前端面包屑使用
  [[nodiscard]] Result<std::vector<Folder>> GetPath(const Actor& actor, std::string_view folder_id);

  [[nodiscard]] Result<Folder> Rename(const Actor& actor, std::string_view folder_id,
                                      std::string_view new_name);

  [[nodiscard]] Result<Folder> Move(const Actor& actor, std::string_view folder_id,
                                    std::string_view new_parent_id);

  [[nodiscard]] Status SoftDelete(const Actor& actor, std::string_view folder_id);

 private:
  // 读取并做权限检查；不存在或无权限统一返回 RESOURCE_NOT_FOUND，避免泄露资源是否存在
  [[nodiscard]] Result<Folder> LoadAccessible(const Actor& actor, std::string_view folder_id,
                                              ResourceAction action);

  FolderRepository& folders_;
  const ResourcePermissionChecker& permissions_;
};

}  // namespace lan_file_hub
