// 用户仓储。所有 SQL 参数化（docs/security.md），不做字符串拼接。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "lan_file_hub/common/result.h"
#include "lan_file_hub/domain/user.h"
#include "lan_file_hub/infrastructure/database.h"

namespace lan_file_hub {

class UserRepository {
 public:
  explicit UserRepository(ConnectionPool& pool) : pool_(pool) {}

  // 插入新用户。用户名/邮箱重复时返回 RESOURCE_CONFLICT。
  [[nodiscard]] Result<User> Create(const User& user);

  // 按用户名**或**邮箱查找（登录用，两者都不区分大小写）。
  [[nodiscard]] Result<std::optional<User>> FindByUsernameOrEmail(std::string_view identifier);

  [[nodiscard]] Result<std::optional<User>> FindById(std::string_view id);

  [[nodiscard]] Result<std::optional<User>> FindByUsername(std::string_view username);

  [[nodiscard]] Status UpdateStatus(std::string_view id, UserStatus status);

  // 按增量调整已用空间（阶段六起由文件/版本事务调用；delta 可为负）。
  // 只在**同一个事务里已经锁住用户行**的前提下才是安全的，
  // 因此文件相关的调用走 FileRepository 的事务内联实现，这里供其它场景使用。
  [[nodiscard]] Status AddUsedStorage(std::string_view id, std::int64_t delta_bytes);

  // 按「所有文件版本大小之和」重算已用空间并写回，返回重算后的值。
  // 口径：包含回收站文件与历史版本（docs/data-model.md §存储统计和配额）。
  // 统计字段只是缓存，定期任务用这个口径校准（阶段十一）。
  [[nodiscard]] Result<std::uint64_t> RecalculateUsedStorage(std::string_view id);

  // 仅供测试使用：清空 users 表。
  [[nodiscard]] Status DeleteAllForTesting();
  // 仅供测试：直接改配额（验证上传中途配额被调小的场景）
  // 管理员用户列表（阶段十三）。search 同时匹配用户名与邮箱；
  // 排序列由调用方从**白名单**里选，仓储这里再兜一层，绝不接受任意输入。
  struct UserQuery {
    std::string search;
    std::string status;  // 空 = 全部；ACTIVE / DISABLED
  };
  [[nodiscard]] Result<std::vector<User>> List(const UserQuery& query, std::string_view sort_column,
                                               bool descending, int limit, int offset);
  [[nodiscard]] Result<std::uint64_t> Count(const UserQuery& query);
  [[nodiscard]] Status SetQuota(std::string_view id, std::uint64_t quota_bytes);
  // 在线可见性（五阶段改造 ⑤：广场）：是否让别人看到自己在线（默认可见）
  [[nodiscard]] Status SetPresenceVisible(std::string_view id, bool visible);
  [[nodiscard]] Status SetQuotaForTesting(std::string_view id, std::uint64_t quota_bytes);

 private:
  ConnectionPool& pool_;
};

}  // namespace lan_file_hub
