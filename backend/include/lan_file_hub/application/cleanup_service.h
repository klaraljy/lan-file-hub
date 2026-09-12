// 后台清理服务（阶段八起步，阶段十一扩展）。
//
// 职责：
//   1. 处理 cleanup_tasks：删除已经没人引用的对象，失败按指数退避重试。
//   2. 定期把超过保留期的回收站条目永久删除。
//
// 设计约束（AGENTS.md §5、docs/recovery.md）：
//   * 所有后台线程必须可停止：用 std::jthread + stop_token，析构时有序退出。
//   * 不在锁内做磁盘或网络 IO；每个任务独立处理，一条失败不影响其他条。
//   * 删除前必须复查引用计数：对象可能被其他文件/用户的版本引用（内容寻址）。
//   * 数据库状态与磁盘不一致时，**只重试磁盘操作**，不把数据库改成"已删除"。
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include "lan_file_hub/application/trash_service.h"
#include "lan_file_hub/infrastructure/audit_repository.h"
#include "lan_file_hub/infrastructure/file_repository.h"
#include "lan_file_hub/infrastructure/upload_session_repository.h"
#include "lan_file_hub/storage/object_storage.h"

namespace lan_file_hub {

struct CleanupServiceOptions {
  std::chrono::milliseconds poll_interval{2000};
  std::int32_t batch_size{32};
  std::int32_t max_attempts{5};
  // 阶段十一：tmp/ 下超过这个年龄的文件视为残留（默认 24 小时，与会话 TTL 对齐）
  std::int64_t stale_temp_seconds{24 * 60 * 60};
  // 每轮一致性扫描最多检查多少个对象 / 多少个哈希（避免一轮扫全库）
  int consistency_scan_limit{500};
  // 每隔多少个轮询周期校准一次用户存储统计（默认约 10 分钟一次）
  int stats_recalibrate_every_ticks{300};
  // 指标刷新频率（阶段十三）：存储用量要扫表，不能每个 tick 都做
  int metrics_every_ticks{15};
  // 每隔多少个轮询周期做一次回收站过期清理（默认约 1 分钟一次）
  int trash_sweep_every_ticks{30};
  bool enabled{true};
};

struct CleanupStats {
  std::uint64_t objects_deleted{0};
  std::uint64_t objects_skipped{0};
  std::uint64_t tasks_failed{0};
  std::uint64_t trash_purged{0};
  std::uint64_t sessions_expired{0};
  std::uint64_t orphan_objects{0};
  std::uint64_t missing_objects{0};
  std::uint64_t stats_corrected_users{0};
  std::uint64_t ticks{0};
};

class CleanupService {
 public:
  CleanupService(CleanupTaskRepository& tasks, FileRepository& files, ObjectStorage& storage,
                 TrashService& trash, UploadSessionRepository& sessions,
                 CleanupServiceOptions options = {});
  ~CleanupService();

  CleanupService(const CleanupService&) = delete;
  CleanupService& operator=(const CleanupService&) = delete;

  void Start();
  void Stop();

  // 跑一轮：处理到期任务；force_trash_sweep 为真时顺带做回收站过期清理。
  // 测试直接调用它，不用等后台线程。
  void RunOnce(bool force_trash_sweep = false);

  [[nodiscard]] CleanupStats stats() const;

 private:
  void Loop(std::stop_token token);

  CleanupTaskRepository& tasks_;
  FileRepository& files_;
  ObjectStorage& storage_;
  TrashService& trash_;
  UploadSessionRepository& sessions_;
  CleanupServiceOptions options_;
  std::jthread worker_;
  std::atomic<bool> started_{false};
  std::atomic<std::uint64_t> objects_deleted_{0};
  std::atomic<std::uint64_t> objects_skipped_{0};
  std::atomic<std::uint64_t> tasks_failed_{0};
  std::atomic<std::uint64_t> trash_purged_{0};
  std::atomic<std::uint64_t> sessions_expired_{0};
  std::atomic<std::uint64_t> orphan_objects_{0};
  std::atomic<std::uint64_t> missing_objects_{0};
  std::atomic<std::uint64_t> stats_corrected_users_{0};
  std::atomic<std::uint64_t> ticks_{0};
};

}  // namespace lan_file_hub
