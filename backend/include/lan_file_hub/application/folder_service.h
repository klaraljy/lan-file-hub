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
#include "lan_file_hub/infrastructure/file_repository.h"
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

/**
 * 「这个文件夹里都有哪些文件」的扁平清单（2026-10-03 用户要求：
 * "我是想一同下载文件夹里面的东西，而不是打包成 zip"）。
 *
 * 用途：在文件夹上点「下载里面的东西」时，前端拿到**每个文件的 id**，
 * 然后逐个走既有的"一次性票据 + 浏览器原生下载"路径 —— 不打包、不占额外磁盘、
 * 断一个不影响其它，手机端与电脑端用的是同一套。
 *
 * `path` 是相对被点目录的路径（例如「子目录/1.mp4」），只用于界面显示与重名区分。
 */
struct FolderFileEntry {
  std::string file_id;
  std::string path;
  std::string name;
  std::uint64_t size_bytes{0};
};

struct FolderFileList {
  std::uint64_t file_count{0};
  std::uint64_t total_bytes{0};
  /** 逐层列出的文件（按路径排序） */
  std::vector<FolderFileEntry> items;
  /** 超过上限被截断（界面要提示"先只下了前 N 个"） */
  bool truncated{false};
};

/**
 * 一次最多返回多少个文件。
 * ⛔ 不是分页：批量下载要一口气拿全，分页反而让"下全"变成多轮。
 * 真超过这个数（一万个文件）说明该分子目录下载了，界面会明说。
 */
constexpr std::size_t kFolderFileListLimit = 2000;

class FolderService {
 public:
  FolderService(FolderRepository& folders, FileRepository& files,
                const ResourcePermissionChecker& permissions);

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

  /**
   * 这个目录（含所有子目录）里的全部文件，供「一同下载」使用。
   * 要求对目录有 READ 权限；无权限与不存在同样返回 RESOURCE_NOT_FOUND。
   */
  [[nodiscard]] Result<FolderFileList> ListFilesRecursive(const Actor& actor,
                                                          std::string_view folder_id);

 private:
  // 读取并做权限检查；不存在或无权限统一返回 RESOURCE_NOT_FOUND，避免泄露资源是否存在
  [[nodiscard]] Result<Folder> LoadAccessible(const Actor& actor, std::string_view folder_id,
                                              ResourceAction action);

  FolderRepository& folders_;
  FileRepository& files_;
  const ResourcePermissionChecker& permissions_;
};

}  // namespace lan_file_hub
