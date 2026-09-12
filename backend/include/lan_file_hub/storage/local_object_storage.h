// 本地文件系统对象存储。
//
// 目录布局（都在 STORAGE_ROOT 之下）：
//   objects/<sha256 前两位>/<sha256>   最终对象，内容寻址
//   tmp/<uuid>                         写入中的临时对象
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "lan_file_hub/storage/object_storage.h"

namespace lan_file_hub {

class LocalObjectStorage final : public ObjectStorage {
 public:
  // root 必须是已存在（或可创建）的目录；构造时会创建 objects/ 与 tmp/
  [[nodiscard]] static Result<std::shared_ptr<LocalObjectStorage>> Create(
      const std::filesystem::path& root);

  [[nodiscard]] Result<std::unique_ptr<ObjectWriter>> CreateWriter() override;
  [[nodiscard]] Status Commit(std::string_view temp_key, std::string_view final_key) override;
  [[nodiscard]] Status Delete(std::string_view key) override;
  [[nodiscard]] bool Exists(std::string_view key) const override;
  [[nodiscard]] Result<std::uint64_t> Size(std::string_view key) const override;
  [[nodiscard]] Result<std::string> ResolveDownloadPath(std::string_view key) const override;
  // 阶段十一：列出 tmp/ 下超过指定年龄的文件（崩溃残留/超时会话的临时文件）
  [[nodiscard]] Result<std::vector<std::string>> ListStaleTempKeys(
      std::int64_t min_age_seconds) const override;

  [[nodiscard]] Result<std::vector<std::string>> ListObjectKeys() const override;

  [[nodiscard]] Result<std::uint64_t> AvailableBytes() const override;

  [[nodiscard]] Result<std::unique_ptr<ObjectWriter>> CreateChunkWriter(
      std::string_view upload_id, std::uint32_t chunk_index) override;
  [[nodiscard]] Result<std::string> MergeChunksToTemp(std::string_view upload_id,
                                                      std::uint32_t total_chunks) override;
  [[nodiscard]] Status DeleteChunks(std::string_view upload_id) override;
  [[nodiscard]] bool ChunkExists(std::string_view upload_id,
                                 std::uint32_t chunk_index) const override;

  [[nodiscard]] const std::filesystem::path& root() const { return root_; }

  // 把 key 解析为绝对路径，并确认结果仍在存储根目录内。
  // 所有落盘操作都必须先过这里。
  [[nodiscard]] Result<std::filesystem::path> ResolveKey(std::string_view key) const;

 private:
  explicit LocalObjectStorage(std::filesystem::path root) : root_(std::move(root)) {}

  std::filesystem::path root_;
};

}  // namespace lan_file_hub
