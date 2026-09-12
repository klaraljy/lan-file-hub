#include "lan_file_hub/storage/local_object_storage.h"

#include <system_error>
#include <utility>
#include <vector>

#include "lan_file_hub/common/uuid.h"

namespace lan_file_hub {
namespace {

constexpr std::size_t kWriteBufferBytes = 1U << 20;  // 1 MiB

class LocalObjectWriter final : public ObjectWriter {
 public:
  LocalObjectWriter(std::filesystem::path path, std::string key)
      : path_(std::move(path)), key_(std::move(key)) {
    stream_.open(path_, std::ios::binary | std::ios::trunc);
  }

  ~LocalObjectWriter() override {
    if (stream_.is_open()) {
      stream_.close();
    }
  }

  [[nodiscard]] Status Write(const char* data, std::size_t length) override {
    if (!stream_.is_open()) {
      return Status(Error{"STORAGE_UNAVAILABLE", "对象写入流未打开"});
    }
    if (length == 0) {
      return Status::Success();
    }
    stream_.write(data, static_cast<std::streamsize>(length));
    if (!stream_) {
      return Status(Error{"STORAGE_FULL", "写入对象失败（可能磁盘空间不足）"});
    }
    bytes_written_ += length;
    return Status::Success();
  }

  [[nodiscard]] Status Finish() override {
    if (!stream_.is_open()) {
      return Status(Error{"STORAGE_UNAVAILABLE", "对象写入流未打开"});
    }
    stream_.flush();
    if (!stream_) {
      return Status(Error{"STORAGE_FULL", "刷新对象失败（可能磁盘空间不足）"});
    }
    stream_.close();
    if (!stream_) {
      return Status(Error{"STORAGE_UNAVAILABLE", "关闭对象文件失败"});
    }
    return Status::Success();
  }

  [[nodiscard]] std::string_view temp_key() const override { return key_; }
  [[nodiscard]] std::uint64_t bytes_written() const override { return bytes_written_; }

 private:
  std::filesystem::path path_;
  std::string key_;
  std::ofstream stream_;
  std::uint64_t bytes_written_{0};
};

}  // namespace

Result<std::shared_ptr<LocalObjectStorage>> LocalObjectStorage::Create(
    const std::filesystem::path& root) {
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  if (ec) {
    return Error{"STORAGE_UNAVAILABLE", "无法创建存储根目录", ec.message()};
  }
  // 根目录必须能取到绝对路径，否则后续的包含性检查没有意义
  const auto absolute = std::filesystem::absolute(root, ec).lexically_normal();
  if (ec) {
    return Error{"STORAGE_UNAVAILABLE", "无法规范化存储根目录", ec.message()};
  }

  std::filesystem::create_directories(absolute / "objects", ec);
  if (ec) {
    return Error{"STORAGE_UNAVAILABLE", "无法创建 objects 目录", ec.message()};
  }
  std::filesystem::create_directories(absolute / "tmp", ec);
  if (ec) {
    return Error{"STORAGE_UNAVAILABLE", "无法创建 tmp 目录", ec.message()};
  }

  return std::shared_ptr<LocalObjectStorage>(new LocalObjectStorage(absolute));
}

Result<std::filesystem::path> LocalObjectStorage::ResolveKey(std::string_view key) const {
  const auto valid = ValidateObjectKey(key);
  if (!valid.Ok()) {
    return valid.GetError();
  }

  std::error_code ec;
  const auto candidate = (root_ / std::filesystem::path(std::string(key))).lexically_normal();
  const auto relative = std::filesystem::relative(candidate, root_, ec);
  // 注意：Windows 上 path::native() 是 wstring，这里统一用 generic_string() 比较
  const std::string relative_text = relative.generic_string();
  if (ec || relative_text.empty() || relative_text.rfind("..", 0) == 0) {
    // 规范化之后跑到根目录外面了
    return Error{"INVALID_ARGUMENT", "对象 Key 越出存储根目录"};
  }
  return candidate;
}

Result<std::unique_ptr<ObjectWriter>> LocalObjectStorage::CreateWriter() {
  const std::string key = "tmp/" + GenerateUuidV4();
  auto path = ResolveKey(key);
  if (!path) {
    return path.GetError();
  }

  auto writer = std::make_unique<LocalObjectWriter>(path.Value(), key);
  if (!writer) {
    return Error{"STORAGE_UNAVAILABLE", "无法创建对象写入器"};
  }
  return std::unique_ptr<ObjectWriter>(writer.release());
}

Status LocalObjectStorage::Commit(std::string_view temp_key, std::string_view final_key) {
  auto temp_path = ResolveKey(temp_key);
  if (!temp_path) {
    return Status(temp_path.GetError());
  }
  auto final_path = ResolveKey(final_key);
  if (!final_path) {
    return Status(final_path.GetError());
  }

  std::error_code ec;
  if (std::filesystem::exists(final_path.Value(), ec)) {
    // 内容寻址：同 key 即同内容。目标已存在时直接丢弃临时对象。
    std::filesystem::remove(temp_path.Value(), ec);
    return Status::Success();
  }

  std::filesystem::create_directories(final_path.Value().parent_path(), ec);
  if (ec) {
    return Status(Error{"STORAGE_UNAVAILABLE", "无法创建对象目录", ec.message()});
  }

  std::filesystem::rename(temp_path.Value(), final_path.Value(), ec);
  if (ec) {
    return Status(Error{"STORAGE_UNAVAILABLE", "提交对象失败", ec.message()});
  }
  return Status::Success();
}

Status LocalObjectStorage::Delete(std::string_view key) {
  auto path = ResolveKey(key);
  if (!path) {
    return Status(path.GetError());
  }
  std::error_code ec;
  std::filesystem::remove(path.Value(), ec);
  if (ec) {
    // 删除失败要如实上报，不能静默吞掉（docs/security.md 与阶段八的要求）
    return Status(Error{"STORAGE_UNAVAILABLE", "删除对象失败", ec.message()});
  }
  return Status::Success();
}

bool LocalObjectStorage::Exists(std::string_view key) const {
  auto path = ResolveKey(key);
  if (!path) {
    return false;
  }
  std::error_code ec;
  return std::filesystem::is_regular_file(path.Value(), ec);
}

Result<std::uint64_t> LocalObjectStorage::Size(std::string_view key) const {
  auto path = ResolveKey(key);
  if (!path) {
    return path.GetError();
  }
  std::error_code ec;
  const auto size = std::filesystem::file_size(path.Value(), ec);
  if (ec) {
    return Error{"RESOURCE_NOT_FOUND", "对象不存在或无法读取", ec.message()};
  }
  return static_cast<std::uint64_t>(size);
}

Result<std::string> LocalObjectStorage::ResolveDownloadPath(std::string_view key) const {
  auto path = ResolveKey(key);
  if (!path) {
    return path.GetError();
  }
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path.Value(), ec)) {
    return Error{"RESOURCE_NOT_FOUND", "对象文件不存在"};
  }
  return path.Value().string();
}

// ---- 分块上传 ---------------------------------------------------------------

Result<std::unique_ptr<ObjectWriter>> LocalObjectStorage::CreateChunkWriter(
    std::string_view upload_id, std::uint32_t chunk_index) {
  auto key = MakeChunkKey(upload_id, chunk_index);
  if (!key) {
    return key.GetError();
  }
  auto path = ResolveKey(key.Value());
  if (!path) {
    return path.GetError();
  }

  std::error_code ec;
  std::filesystem::create_directories(path.Value().parent_path(), ec);
  if (ec) {
    return Error{"STORAGE_UNAVAILABLE", "无法创建分块目录", ec.message()};
  }
  return std::unique_ptr<ObjectWriter>(new LocalObjectWriter(path.Value(), key.Value()));
}

bool LocalObjectStorage::ChunkExists(std::string_view upload_id, std::uint32_t chunk_index) const {
  auto key = MakeChunkKey(upload_id, chunk_index);
  if (!key) {
    return false;
  }
  auto path = ResolveKey(key.Value());
  if (!path) {
    return false;
  }
  std::error_code ec;
  return std::filesystem::is_regular_file(path.Value(), ec);
}

Result<std::string> LocalObjectStorage::MergeChunksToTemp(std::string_view upload_id,
                                                          std::uint32_t total_chunks) {
  // 合并到临时对象；调用方算完哈希、校验通过后再 Commit 到内容寻址的最终 key。
  const std::string temp_key = "tmp/" + GenerateUuidV4();
  auto temp_path = ResolveKey(temp_key);
  if (!temp_path) {
    return temp_path.GetError();
  }

  constexpr std::size_t kBufferBytes = 1U << 20;  // 1 MiB，全程流式
  std::vector<char> buffer(kBufferBytes);

  const auto fail_with = [&](Error error) -> Result<std::string> {
    std::error_code cleanup;
    std::filesystem::remove(temp_path.Value(), cleanup);
    return error;
  };

  std::ofstream out(temp_path.Value(), std::ios::binary | std::ios::trunc);
  if (!out) {
    return Error{"STORAGE_UNAVAILABLE", "无法创建合并目标文件"};
  }

  for (std::uint32_t index = 0; index < total_chunks; ++index) {
    auto chunk_key = MakeChunkKey(upload_id, index);
    if (!chunk_key) {
      out.close();
      return fail_with(Error{"UPLOAD_INCOMPLETE", "分块路径不合法"});
    }
    auto chunk_path = ResolveKey(chunk_key.Value());
    if (!chunk_path) {
      out.close();
      return fail_with(Error{"UPLOAD_INCOMPLETE", "分块路径不合法"});
    }

    std::ifstream in(chunk_path.Value(), std::ios::binary);
    if (!in) {
      out.close();
      return fail_with(
          Error{"UPLOAD_INCOMPLETE", "缺少分块 " + std::to_string(index) + "，无法合并"});
    }

    while (in) {
      in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      const auto read = in.gcount();
      if (read <= 0)
        break;
      out.write(buffer.data(), read);
      if (!out) {
        in.close();
        out.close();
        return fail_with(Error{"STORAGE_FULL", "合并写入失败（可能磁盘空间不足）"});
      }
    }
    if (in.bad()) {
      in.close();
      out.close();
      return fail_with(Error{"STORAGE_UNAVAILABLE", "读取分块失败"});
    }
  }

  out.flush();
  out.close();
  if (!out) {
    return fail_with(Error{"STORAGE_FULL", "合并落盘失败"});
  }
  return temp_key;
}

Status LocalObjectStorage::DeleteChunks(std::string_view upload_id) {
  auto key = MakeChunkKey(upload_id, 0);
  if (!key) {
    // 会话标识非法时无从下手，但不该让调用方因此失败（此时本来也没有分块）
    return Status::Success();
  }
  auto sample = ResolveKey(key.Value());
  if (!sample) {
    return Status::Success();
  }

  std::error_code ec;
  std::filesystem::remove_all(sample.Value().parent_path(), ec);
  if (ec) {
    return Status(Error{"STORAGE_UNAVAILABLE", "清理分块失败", ec.message()});
  }
  return Status::Success();
}

Result<std::vector<std::string>> LocalObjectStorage::ListObjectKeys() const {
  const auto probe = ResolveKey("objects/aa/x");
  if (!probe) {
    return probe.GetError();
  }
  const auto objects_dir = probe.Value().parent_path().parent_path();

  std::error_code ec;
  if (!std::filesystem::is_directory(objects_dir, ec)) {
    return std::vector<std::string>{};
  }

  std::vector<std::string> keys;
  for (auto it = std::filesystem::recursive_directory_iterator(
           objects_dir, std::filesystem::directory_options::skip_permission_denied, ec);
       it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) {
      break;
    }
    if (!it->is_regular_file(ec)) {
      continue;
    }
    const auto relative = std::filesystem::relative(it->path(), root_, ec);
    if (ec) {
      continue;
    }
    const std::string key = relative.generic_string();
    if (ValidateObjectKey(key).Ok()) {
      keys.push_back(key);
    }
  }
  return keys;
}
// ---- 资源保护（阶段十二）----------------------------------------------------

Result<std::uint64_t> LocalObjectStorage::AvailableBytes() const {
  std::error_code ec;
  // 直接问操作系统，不缓存：缓存会让"磁盘快满"这个判断滞后
  const auto info = std::filesystem::space(root_, ec);
  if (ec) {
    return Error{"STORAGE_UNAVAILABLE", "无法读取磁盘可用空间", ec.message()};
  }
  return static_cast<std::uint64_t>(info.available);
}
// ---- 后台清理（阶段十一）----------------------------------------------------

Result<std::vector<std::string>> LocalObjectStorage::ListStaleTempKeys(
    std::int64_t min_age_seconds) const {
  const auto root = ResolveKey("tmp/x");
  if (!root) {
    return root.GetError();
  }
  const auto temp_dir = root.Value().parent_path();

  std::error_code ec;
  if (!std::filesystem::is_directory(temp_dir, ec)) {
    return std::vector<std::string>{};  // 还没有临时目录：不算错误
  }

  const auto now = std::filesystem::file_time_type::clock::now();
  const auto max_age = std::chrono::seconds(std::max<std::int64_t>(min_age_seconds, 0));

  std::vector<std::string> keys;
  for (auto it = std::filesystem::recursive_directory_iterator(
           temp_dir, std::filesystem::directory_options::skip_permission_denied, ec);
       it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) {
      break;  // 权限或 IO 问题：返回已收集到的部分，交给后台下一轮继续
    }
    if (!it->is_regular_file(ec)) {
      continue;
    }
    const auto written = it->last_write_time(ec);
    if (ec) {
      continue;
    }
    if (now - written < max_age) {
      continue;  // 还年轻：可能正在被上传使用
    }
    const auto relative = std::filesystem::relative(it->path(), root_, ec);
    if (ec) {
      continue;
    }
    const std::string key = relative.generic_string();  // 已相对存储根，含 tmp/ 前缀
    if (ValidateObjectKey(key).Ok()) {
      keys.push_back(key);
    }
  }
  return keys;
}
}  // namespace lan_file_hub
