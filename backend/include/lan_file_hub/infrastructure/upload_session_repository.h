// 上传会话与分块仓储。
//
// 「完成上传」的原子性靠一条 compare-and-set 语句实现（ClaimForMerge）：
// 只有把状态从 INITIATED/UPLOADING 改成 MERGING 成功的那个请求才会去合并，
// 其余并发请求会看到 MERGING 并被拒绝。合并过程**不持有数据库锁**。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lan_file_hub/common/result.h"
#include "lan_file_hub/domain/upload_session.h"
#include "lan_file_hub/infrastructure/database.h"

namespace lan_file_hub {

enum class ChunkUpsertResult {
  kInserted,        // 新写入
  kAlreadyPresent,  // 已存在且内容一致（幂等）
  kConflict,        // 已存在但内容不同
};

class UploadSessionRepository {
 public:
  explicit UploadSessionRepository(ConnectionPool& pool) : pool_(pool) {}

  // ttl_seconds 由数据库用 now() + interval 计算过期时间，
  // 避免在 C++ 里格式化时间（时区、精度都更可靠）
  [[nodiscard]] Result<UploadSession> Create(const UploadSession& session,
                                             std::uint32_t ttl_seconds);

  // 已过期的会话会把状态落成 EXPIRED 后再返回
  [[nodiscard]] Result<std::optional<UploadSession>> FindById(std::string_view id);

  // 原子地把会话从进行中状态抢到 MERGING；返回 false 表示没抢到
  [[nodiscard]] Result<bool> ClaimForMerge(std::string_view id);

  [[nodiscard]] Status UpdateStatus(std::string_view id, UploadStatus status,
                                    std::optional<std::string> error_code = std::nullopt);

  // 标记完成并记录生成的文件（让 complete 可以幂等重复调用）
  [[nodiscard]] Status AttachFileAndComplete(std::string_view id, std::string_view file_id);

  [[nodiscard]] Result<ChunkUpsertResult> UpsertChunk(const UploadChunk& chunk);

  // 查某个分块的记录（重复上传时用来判断内容是否一致）
  [[nodiscard]] Result<std::optional<UploadChunk>> FindChunk(std::string_view session_id,
                                                             std::uint32_t chunk_index);

  [[nodiscard]] Result<std::vector<std::uint32_t>> ListChunkIndexes(std::string_view session_id);

  [[nodiscard]] Result<std::uint64_t> CountChunks(std::string_view session_id);

  [[nodiscard]] Result<std::uint64_t> SumChunkBytes(std::string_view session_id);

  // 并发上限用：进行中的会话数
  [[nodiscard]] Result<std::uint64_t> CountInProgressByUser(std::string_view user_id);
  [[nodiscard]] Result<std::uint64_t> CountInProgressGlobal();

  /** 该目录里是否已有**还没结束**的同名上传会话。
   *  自动改名（同名加 (1)、(2)…）时必须连"正在传的那个"一起避开，
   *  否则连着建三次会话会都拿到 (1)（2026-10-03 实测踩到）。 */
  [[nodiscard]] Result<bool> ExistsPendingName(std::string_view folder_id,
                                               std::string_view file_name);

  [[nodiscard]] Status DeleteChunks(std::string_view session_id);

  [[nodiscard]] Status DeleteSession(std::string_view session_id);

  // 仅供测试
  [[nodiscard]] Status DeleteAllForTesting();
  // 仅供测试：把过期时间提前到过去（验证续传时的过期处理，不靠等待）
  [[nodiscard]] Status ExpireForTesting(std::string_view session_id);

  // 阶段十一（后台任务）：把已过期的进行中会话落成 EXPIRED，返回这些会话 id。
  // 调用方负责清理它们残留的分块（数据库记录 + 磁盘）。
  [[nodiscard]] Result<std::vector<std::string>> ExpireOverdue(int limit);

 private:
  ConnectionPool& pool_;
};

}  // namespace lan_file_hub
