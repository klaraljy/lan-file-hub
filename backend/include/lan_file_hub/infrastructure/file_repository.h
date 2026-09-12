// 文件仓储。文件和它的首个版本必须在一个事务里落库。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lan_file_hub/common/pagination.h"
#include "lan_file_hub/common/result.h"
#include "lan_file_hub/domain/file.h"
#include "lan_file_hub/infrastructure/database.h"

namespace lan_file_hub {

// 已经存在的对象（阶段六的去重预检查用）。
struct ExistingObject {
  std::string content_hash;
  std::uint64_t size_bytes{0};
  std::string storage_key;
  std::string file_id;    // 该用户哪个文件引用了它
  std::string file_name;  // 便于前端提示「与 xxx 内容相同」
};

class FileRepository {
 public:
  explicit FileRepository(ConnectionPool& pool) : pool_(pool) {}

  // 创建文件并写入首个版本；事务内完成，任一失败整体回滚。
  //
  // 配额检查也在这个事务里：先对用户行加排他锁再比对用量，
  // 因此两个并发上传不可能同时通过检查（阶段六要求「防止并发上传绕过配额」）。
  // 超出配额返回 QUOTA_EXCEEDED。
  [[nodiscard]] Result<File> CreateWithFirstVersion(const File& file, const FileVersion& version);

  [[nodiscard]] Result<std::optional<File>> FindById(std::string_view id);

  // 只取所有者：回收站与权限检查需要在不加载完整记录的情况下判断归属
  [[nodiscard]] Result<std::optional<std::string>> FindOwnerId(std::string_view id);

  // 软删除：状态改为 TRASHED 并记 deleted_at（幂等；已经删除的不再改动）
  [[nodiscard]] Status SoftDelete(std::string_view id);

  // ---- 版本与乐观并发（阶段七）----

  // 给已有文件新增一个版本：同一事务内
  //   1) SELECT ... FOR UPDATE 锁住文件行
  //   2) 校验 If-Match（expected_version_id 为空表示客户端没声明，按追加处理）
  //   3) 版本号 = 当前最大 + 1，插入新版本并切换 current_version_id
  //   4) 配额检查 + 累加 used_storage_bytes
  // 版本号只会增加，**不会覆盖任何已有版本行或对象**。
  // 冲突时返回 FILE_VERSION_CONFLICT，details 里带 current_version_id。
  [[nodiscard]] Result<File> AddVersion(std::string_view file_id, const FileVersion& version,
                                        std::optional<std::string> expected_version_id);

  // 历史版本列表（按版本号从新到旧）
  [[nodiscard]] Result<std::vector<FileVersion>> ListVersions(std::string_view file_id);

  [[nodiscard]] Result<std::optional<FileVersion>> FindVersion(std::string_view file_id,
                                                               std::string_view version_id);

  [[nodiscard]] Result<std::optional<FileVersion>> FindCurrentVersion(std::string_view file_id);

  // 同目录下是否已有同名有效文件
  [[nodiscard]] Result<bool> ExistsWithName(std::string_view folder_id, std::string_view name);

  [[nodiscard]] Result<std::vector<File>> ListByFolder(std::string_view folder_id,
                                                       const Pagination& pagination,
                                                       const std::string& order_by);

  [[nodiscard]] Result<std::uint64_t> CountByFolder(std::string_view folder_id);

  [[nodiscard]] Result<std::uint64_t> SumSizeByOwner(std::string_view owner_id);

  // 阶段十一（定期校准）：按「所有文件版本大小之和」重算**全部**用户的 used_storage_bytes，
  // 返回被修正的用户数（0 表示统计没有偏差）。口径与迁移 0005 的回填一致。
  [[nodiscard]] Result<std::uint64_t> RecalculateAllUsedStorage();

  // ---- 去重（阶段六）----

  // 查该用户自己是否已经有这份内容。
  // 只按 owner 查是刻意为之：不去回答「系统里有没有这个哈希」，
  // 避免把上传接口变成「猜文件是否存在」的探测工具（docs/security.md）。
  [[nodiscard]] Result<std::optional<ExistingObject>> FindOwnedObjectByHash(
      std::string_view owner_id, std::string_view content_hash);

  // 全库范围内有多少文件版本引用这份内容（不区分用户）。
  // 删除物理对象前必须先确认为 0：内容寻址意味着不同用户的文件可能指向同一个对象。
  [[nodiscard]] Result<std::uint64_t> CountObjectReferences(std::string_view content_hash);

  // 阶段十一：列出所有被版本引用的内容哈希（一致性扫描用）。
  // 只用于**告警**：有引用但磁盘对象缺失时绝不能自动删数据库记录。
  [[nodiscard]] Result<std::vector<std::string>> ListDistinctContentHashes(int limit);

  // 系统级存储统计（阶段十三）：所有文件版本占用的字节总和，供指标与管理端使用
  [[nodiscard]] Result<std::uint64_t> SumAllVersionBytes();
  // 系统级计数（阶段十三管理端）：文件总数与版本总数
  [[nodiscard]] Result<std::uint64_t> CountAllFiles();
  [[nodiscard]] Result<std::uint64_t> CountAllVersions();

  // 仅供测试：清空 files（file_versions 会级联删除）
  [[nodiscard]] Status DeleteAllForTesting();
  // 仅供测试：删除一条文件记录（版本级联删除）。
  // 阶段八的正式删除接口会走应用服务，这里只用来验证「引用计数降为 0 之前不动物理对象」。
  [[nodiscard]] Status DeleteByIdForTesting(std::string_view file_id);

 private:
  ConnectionPool& pool_;
};

}  // namespace lan_file_hub
