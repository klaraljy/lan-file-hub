// 对象存储抽象。
//
// 红线（docs/storage.md、AGENTS.md §5）：
//   - 业务层不得直接用 std::filesystem 访问用户路径，一切文件内容都走这里
//   - 对象的物理路径只能由服务端生成，绝不使用用户提供的文件名
//   - 上传必须是流式的，禁止把整个文件读进内存
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "lan_file_hub/common/result.h"

namespace lan_file_hub {

// 流式写入句柄：写入的是**临时对象**，写完后由 Commit 挪到最终 key。
class ObjectWriter {
 public:
  virtual ~ObjectWriter() = default;

  [[nodiscard]] virtual Status Write(const char* data, std::size_t length) = 0;
  // 落盘并关闭；调用后不能再 Write
  [[nodiscard]] virtual Status Finish() = 0;
  [[nodiscard]] virtual std::string_view temp_key() const = 0;
  [[nodiscard]] virtual std::uint64_t bytes_written() const = 0;
};

class ObjectStorage {
 public:
  virtual ~ObjectStorage() = default;

  // 开一个流式写入器（临时对象，key 由实现生成）
  [[nodiscard]] virtual Result<std::unique_ptr<ObjectWriter>> CreateWriter() = 0;

  // 把临时对象提交到最终 key。目标已存在时视为成功（内容寻址，同 hash 同内容），
  // 并清理临时对象。
  [[nodiscard]] virtual Status Commit(std::string_view temp_key, std::string_view final_key) = 0;

  [[nodiscard]] virtual Status Delete(std::string_view key) = 0;

  [[nodiscard]] virtual bool Exists(std::string_view key) const = 0;

  [[nodiscard]] virtual Result<std::uint64_t> Size(std::string_view key) const = 0;

  // 取绝对路径，**只给 HTTP 层的流式下载响应使用**。
  // 返回前会再次校验最终的规范化路径仍在存储根目录内。
  [[nodiscard]] virtual Result<std::string> ResolveDownloadPath(std::string_view key) const = 0;

  // ---- 分块上传（阶段五）----

  // 开一个分块的写入器。分块独立落盘，key 由实现根据 upload_id 与 chunk_index 生成。
  [[nodiscard]] virtual Result<std::unique_ptr<ObjectWriter>> CreateChunkWriter(
      std::string_view upload_id, std::uint32_t chunk_index) = 0;

  // 按 chunk_index 升序把所有分块合并到**临时对象**，返回它的 key。
  //
  // 为什么不直接合并到最终 key：最终 key 由合并结果的 SHA-256 决定，
  // 而哈希只有在合并完成后才能算出来。调用方拿到临时对象后：
  //   算哈希 → 校验客户端声明 → Commit(临时 key, 最终 key)。
  // 分块缺失或数量不符时返回错误，且不留下半成品。
  [[nodiscard]] virtual Result<std::string> MergeChunksToTemp(std::string_view upload_id,
                                                              std::uint32_t total_chunks) = 0;

  // 删除某个上传会话的全部分块（完成、取消或失败后调用）
  [[nodiscard]] virtual Status DeleteChunks(std::string_view upload_id) = 0;

  // 某个分块是否已经落盘
  [[nodiscard]] virtual bool ChunkExists(std::string_view upload_id,
                                         std::uint32_t chunk_index) const = 0;

  // ---- 后台清理（阶段十一）----

  // 列出 tmp/ 下**超过指定年龄**的文件 Key。
  // 合并过程中崩溃会留下无引用的 tmp/<uuid>；上传会话超时后 tmp/uploads/<id>/ 也会成为残留。
  // 用「年龄」而不是「当前是否被引用」判定：活跃的上传会持续写入，只有真正停下来的才会变旧；
  // 会话 TTL（默认 24 小时）与这里的阈值对齐，两条清理路径不会互相打架。
  [[nodiscard]] virtual Result<std::vector<std::string>> ListStaleTempKeys(
      std::int64_t min_age_seconds) const = 0;

  // 列出 objects/ 下的**全部**对象 Key（阶段十一的一致性扫描用）。
  // 对象是永久数据，不带年龄过滤；调用方按引用计数决定是否删除。
  [[nodiscard]] virtual Result<std::vector<std::string>> ListObjectKeys() const = 0;

  // ---- 资源保护（阶段十二）----

  // 存储根所在卷的可用字节数。上传开始前与合并前都要检查：
  // 磁盘写满时写入会失败，与其传完几十 GB 再报错，不如提前拒绝。
  [[nodiscard]] virtual Result<std::uint64_t> AvailableBytes() const = 0;
};

// 对象 Key 规则（docs/storage.md §路径安全）
inline constexpr std::size_t kMaxObjectKeyLength = 512;
inline constexpr std::size_t kTempKeyPrefixLength = 4;  // "tmp/"

// 校验 key 是否合法：非空、长度受限、相对路径、不含 ".."、只含安全字符。
[[nodiscard]] Status ValidateObjectKey(std::string_view key);

// 由 SHA-256 生成内容寻址的最终 key：objects/<前两位>/<完整 hash>
[[nodiscard]] Result<std::string> MakeObjectKey(std::string_view sha256_hex);

// 分块的对象 Key：tmp/uploads/<upload_id>/<index>
[[nodiscard]] Result<std::string> MakeChunkKey(std::string_view upload_id,
                                               std::uint32_t chunk_index);

}  // namespace lan_file_hub
