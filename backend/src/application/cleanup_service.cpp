#include "lan_file_hub/application/cleanup_service.h"

#include "lan_file_hub/common/metrics.h"

#include <string>
#include <string_view>
#include <utility>

#include <spdlog/spdlog.h>

namespace lan_file_hub {

CleanupService::CleanupService(CleanupTaskRepository& tasks, FileRepository& files,
                               ObjectStorage& storage, TrashService& trash,
                               UploadSessionRepository& sessions, CleanupServiceOptions options)
    : tasks_(tasks),
      files_(files),
      storage_(storage),
      trash_(trash),
      sessions_(sessions),
      options_(options) {}

CleanupService::~CleanupService() {
  Stop();
}

void CleanupService::Start() {
  if (!options_.enabled || started_.exchange(true)) {
    return;
  }
  // 启动恢复：把上次进程遗留的待处理任务立即置为到期（失败不阻塞启动）
  (void)tasks_.ResetPendingForStartup();
  worker_ = std::jthread([this](std::stop_token token) { Loop(token); });
  spdlog::info("后台清理任务已启动 | poll_ms={} | batch={} | max_attempts={}",
               options_.poll_interval.count(), options_.batch_size, options_.max_attempts);
}

void CleanupService::Stop() {
  if (!started_.exchange(false)) {
    return;
  }
  worker_.request_stop();
  if (worker_.joinable()) {
    worker_.join();
  }
  spdlog::info("后台清理任务已停止");
}

void CleanupService::Loop(std::stop_token token) {
  while (!token.stop_requested()) {
    RunOnce(false);
    // 可中断的等待：request_stop 后立刻返回，不用等满一个周期
    std::this_thread::sleep_for(options_.poll_interval);
  }
}

void CleanupService::RunOnce(bool force_trash_sweep) {
  const std::uint64_t tick = ticks_.fetch_add(1) + 1;

  auto due = tasks_.ClaimDue(options_.batch_size);
  if (!due) {
    spdlog::warn("领取清理任务失败 | error={}", due.GetError().detail);
  } else {
    for (const auto& task : due.Value()) {
      // 内容寻址：objects/<xx>/<hash> 可能仍被其他文件或用户的版本引用，
      // 删除前必须复查引用计数（阶段六的约定：为 0 才允许删物理对象）
      const std::string_view key = task.storage_key;
      if (key.rfind("objects/", 0) == 0) {
        const auto slash = key.find_last_of('/');
        const std::string hash(key.substr(slash == std::string_view::npos ? 0 : slash + 1));
        auto references = files_.CountObjectReferences(hash);
        if (references && references.Value() > 0) {
          (void)tasks_.MarkDone(task.id);
          objects_skipped_.fetch_add(1);
          Metrics().IncrementCounter("lan_file_hub_background_jobs_total",
                                     "后台清理任务处理结果计数", {{"result", "skipped"}});
          spdlog::info("对象仍被引用，跳过删除 | key={} | references={}", task.storage_key,
                       references.Value());
          continue;
        }
      }

      auto deleted = storage_.Delete(task.storage_key);
      if (deleted.Ok()) {
        (void)tasks_.MarkDone(task.id);
        objects_deleted_.fetch_add(1);
        Metrics().IncrementCounter("lan_file_hub_background_jobs_total", "后台清理任务处理结果计数",
                                   {{"result", "deleted"}});
        continue;
      }

      tasks_failed_.fetch_add(1);
      Metrics().IncrementCounter("lan_file_hub_background_jobs_total", "后台清理任务处理结果计数",
                                 {{"result", "failed"}});
      spdlog::warn("对象删除失败，稍后重试 | key={} | attempts={} | error={}", task.storage_key,
                   task.attempts, deleted.GetError().detail);
      (void)tasks_.MarkFailed(task.id, deleted.GetError().code, options_.max_attempts);
    }
  }

  // 指标（阶段十三）：活跃上传会话每 tick 刷一次；存储用量要扫表，按较低频率刷
  if (auto active = sessions_.CountInProgressGlobal(); active) {
    Metrics().SetGauge("lan_file_hub_active_uploads", "当前进行中的上传会话数",
                       static_cast<double>(active.Value()));
  }
  if (options_.metrics_every_ticks > 0 &&
      (tick <= 1 || tick % static_cast<std::uint64_t>(options_.metrics_every_ticks) == 0)) {
    if (auto used = files_.SumAllVersionBytes(); used) {
      Metrics().SetGauge("lan_file_hub_storage_used_bytes", "全部文件版本占用的字节数",
                         static_cast<double>(used.Value()));
    }
    if (auto free_bytes = storage_.AvailableBytes(); free_bytes) {
      Metrics().SetGauge("lan_file_hub_storage_free_bytes", "对象存储所在卷的可用字节数",
                         static_cast<double>(free_bytes.Value()));
    }
  }
  const bool sweep = force_trash_sweep ||
                     (options_.trash_sweep_every_ticks > 0 &&
                      tick % static_cast<std::uint64_t>(options_.trash_sweep_every_ticks) == 0);
  if (sweep) {
    auto purged = trash_.PurgeExpired(options_.batch_size);
    if (!purged) {
      spdlog::warn("回收站过期清理失败 | error={}", purged.GetError().detail);
    } else {
      trash_purged_.fetch_add(purged.Value());
    }

    // 过期上传会话：先落状态再清残留分块（数据库记录 + 磁盘目录）。
    // 顺序不能反——先改状态，中途崩溃重跑也只是多清一次（幂等）。
    auto expired = sessions_.ExpireOverdue(options_.batch_size);
    if (!expired) {
      spdlog::warn("过期上传会话扫描失败 | error={}", expired.GetError().detail);
    } else {
      for (const auto& session_id : expired.Value()) {
        (void)sessions_.DeleteChunks(session_id);
        (void)storage_.DeleteChunks(session_id);
      }
      if (!expired.Value().empty()) {
        spdlog::info("过期上传会话已清理 | count={}", expired.Value().size());
      }
      sessions_expired_.fetch_add(expired.Value().size());
    }

    // 陈旧临时文件：崩溃残留的 tmp/<uuid>、超时会话留下的 tmp/uploads/<id>/。
    // 按年龄判定而不是查引用，活跃上传不会受影响；入队后由同一套重试机制删除。
    auto stale = storage_.ListStaleTempKeys(options_.stale_temp_seconds);
    if (!stale) {
      spdlog::warn("临时文件扫描失败 | error={}", stale.GetError().detail);
    } else if (!stale.Value().empty()) {
      auto queued = tasks_.EnqueueMany(stale.Value(), "stale-temp");
      if (!queued.Ok()) {
        spdlog::warn("陈旧临时文件入队失败 | count={} | error={}", stale.Value().size(),
                     queued.GetError().detail);
      } else {
        spdlog::info("陈旧临时文件已入队清理 | count={}", stale.Value().size());
        objects_skipped_.fetch_add(0);  // 计数保持只增（删除结果在后续轮次体现）
      }
    }

    // ---- 对象与数据库不一致扫描（阶段十一）----
    //
    // 两类问题，处理方式刻意不同：
    //   * 磁盘有对象但没有版本引用 → 交给清理任务删除（内容寻址下没人用就是垃圾）
    //   * 有版本引用但对象缺失 → **只告警**，绝不自动删或改数据库记录：
    //     那可能是唯一副本，误删就是不可逆的数据丢失，必须人工介入。
    auto object_keys = storage_.ListObjectKeys();
    if (!object_keys) {
      spdlog::warn("对象枚举失败 | error={}", object_keys.GetError().detail);
    } else {
      std::size_t orphans = 0;
      int scanned = 0;
      const int limit = options_.consistency_scan_limit;
      for (const auto& key : object_keys.Value()) {
        // 上限约束的是「本轮检查了多少个对象」，不是「发现了多少个孤儿」，
        // 否则库里有大量被引用对象时一轮会扫全表
        if (scanned >= limit) {
          break;
        }
        scanned += 1;
        const auto slash = key.find_last_of('/');
        if (slash == std::string::npos) {
          continue;
        }
        auto references = files_.CountObjectReferences(key.substr(slash + 1));
        if (!references || references.Value() > 0) {
          continue;
        }
        if (tasks_.Enqueue(key, "orphan-object").Ok()) {
          orphans += 1;
        }
      }
      if (orphans > 0) {
        spdlog::info("发现无引用对象，已入队清理 | count={}", orphans);
      }
      orphan_objects_.fetch_add(orphans);
    }

    auto referenced = files_.ListDistinctContentHashes(options_.consistency_scan_limit);
    if (!referenced) {
      spdlog::warn("内容哈希枚举失败 | error={}", referenced.GetError().detail);
    } else {
      std::size_t missing = 0;
      for (const auto& hash : referenced.Value()) {
        auto key = MakeObjectKey(hash);
        if (!key || storage_.Exists(key.Value())) {
          continue;
        }
        missing += 1;
        spdlog::error("对象缺失：数据库有引用但磁盘没有 | hash={} | 只告警，需人工介入", hash);
      }
      missing_objects_.fetch_add(missing);
    }
  }

  // ---- 用户存储统计定期校准（阶段十一）----
  //
  // used_storage_bytes 只是缓存：任何漏加/漏减都会长期存在，所以定期用
  // 「所有文件版本大小之和」这个权威口径重算。只更新有偏差的行，
  // 返回的修正数量即是偏差信号（>0 说明有路径漏记，值得查）。
  if (options_.stats_recalibrate_every_ticks > 0 &&
      tick % static_cast<std::uint64_t>(options_.stats_recalibrate_every_ticks) == 0) {
    auto corrected = files_.RecalculateAllUsedStorage();
    if (!corrected) {
      spdlog::warn("存储统计校准失败 | error={}", corrected.GetError().detail);
    } else if (corrected.Value() > 0) {
      spdlog::warn("存储统计已校准 | 修正用户数={} | 说明有路径漏记，值得排查", corrected.Value());
      stats_corrected_users_.fetch_add(corrected.Value());
    }
  }
}

CleanupStats CleanupService::stats() const {
  CleanupStats stats;
  stats.objects_deleted = objects_deleted_.load();
  stats.objects_skipped = objects_skipped_.load();
  stats.tasks_failed = tasks_failed_.load();
  stats.trash_purged = trash_purged_.load();
  stats.sessions_expired = sessions_expired_.load();
  stats.orphan_objects = orphan_objects_.load();
  stats.missing_objects = missing_objects_.load();
  stats.stats_corrected_users = stats_corrected_users_.load();
  stats.ticks = ticks_.load();
  return stats;
}

}  // namespace lan_file_hub
