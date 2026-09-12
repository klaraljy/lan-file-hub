// 审计日志与异步清理任务的仓储（阶段八）。
//
// 审计日志只写不读：查询接口属于阶段十三的管理员 API。
// 清理任务是「永久删除」与后台扫描的落点，带重试与退避。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lan_file_hub/common/result.h"
#include "lan_file_hub/infrastructure/database.h"

namespace lan_file_hub {

// 一条审计记录。action 用「动词_资源」的形式，例如 delete_file / restore_folder / purge_file。
struct AuditEntry {
  std::string id;
  std::string actor_id;
  std::string action;
  std::string resource_type;  // file / folder / user ...
  std::string resource_id;
  std::string detail_json;  // 已经序列化好的 JSON 对象字符串，内容不得含凭据
  std::string created_at;
};

enum class CleanupStatus { kPending, kDone, kFailed };

[[nodiscard]] std::string ToDbString(CleanupStatus status);

struct CleanupTask {
  std::string id;
  std::string storage_key;
  std::string reason;
  std::int32_t attempts{0};
  CleanupStatus status{CleanupStatus::kPending};
  std::string last_error;
  std::string next_attempt_at;
  std::string created_at;
};

class AuditLogRepository {
 public:
  explicit AuditLogRepository(ConnectionPool& pool) : pool_(pool) {}

  [[nodiscard]] Status Record(const AuditEntry& entry);

  // 供测试与阶段十三的管理员查询使用
  [[nodiscard]] Result<std::vector<AuditEntry>> ListRecent(std::string_view resource_id, int limit);
  [[nodiscard]] Result<std::uint64_t> CountByAction(std::string_view action);

  // 管理员查询（阶段十三）：**只按固定片段拼 SQL，值一律绑定**；
  // 过滤字段全部可选，空串表示不限制。
  struct AuditQuery {
    std::string actor_id;
    std::string action;
    std::string resource_type;
    std::string resource_id;
  };
  [[nodiscard]] Result<std::vector<AuditEntry>> List(const AuditQuery& query, int limit,
                                                     int offset);
  [[nodiscard]] Result<std::uint64_t> Count(const AuditQuery& query);

  [[nodiscard]] Status DeleteAllForTesting();

 private:
  ConnectionPool& pool_;
};

class CleanupTaskRepository {
 public:
  explicit CleanupTaskRepository(ConnectionPool& pool) : pool_(pool) {}

  // 入队一个待删除对象。同一个 Key 已有待处理任务时直接返回成功（幂等）。
  [[nodiscard]] Status Enqueue(std::string_view storage_key, std::string_view reason);

  [[nodiscard]] Status EnqueueMany(const std::vector<std::string>& storage_keys,
                                   std::string_view reason);

  // 取一批到期任务并把它们标记为「已领取」（attempts + 1），避免多个 worker 重复处理
  [[nodiscard]] Result<std::vector<CleanupTask>> ClaimDue(int limit);

  [[nodiscard]] Status MarkDone(std::string_view id);

  // 记录失败并按指数退避安排下次重试；超过 max_attempts 后置为 FAILED（等人工介入）
  [[nodiscard]] Status MarkFailed(std::string_view id, std::string_view error,
                                  std::int32_t max_attempts);

  [[nodiscard]] Result<std::uint64_t> CountByStatus(CleanupStatus status);

  [[nodiscard]] Result<std::vector<CleanupTask>> ListByStatus(CleanupStatus status, int limit);

  [[nodiscard]] Status DeleteAllForTesting();

  // 阶段十一（启动恢复）：把上次进程遗留的待处理任务立即置为到期。
  // 不这么做的话，重启后要等一个退避周期（最长约 17 分钟）才会重新尝试。
  [[nodiscard]] Status ResetPendingForStartup();

 private:
  ConnectionPool& pool_;
};

}  // namespace lan_file_hub
