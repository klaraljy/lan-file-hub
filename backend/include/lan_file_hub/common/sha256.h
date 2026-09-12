// 流式 SHA-256。
//
// 为什么不用一次性接口：上传/下载都必须是流式的，
// 不能把整个文件读进内存（docs/phases/04-*.md 约束、AGENTS.md §5 红线）。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "lan_file_hub/common/result.h"

namespace lan_file_hub {

class Sha256Hasher {
 public:
  Sha256Hasher();
  ~Sha256Hasher();

  Sha256Hasher(const Sha256Hasher&) = delete;
  Sha256Hasher& operator=(const Sha256Hasher&) = delete;
  Sha256Hasher(Sha256Hasher&&) = delete;
  Sha256Hasher& operator=(Sha256Hasher&&) = delete;

  // 追加数据；可重复调用。失败只可能是内部状态错误。
  [[nodiscard]] Status Update(const char* data, std::size_t length);

  // 结束计算并返回 64 位小写十六进制摘要。只能调用一次。
  [[nodiscard]] Result<std::string> HexDigest();

 private:
  void* ctx_{nullptr};  // EVP_MD_CTX*，避免在头文件里暴露 OpenSSL
  bool finished_{false};
};

// 便捷函数：对一段内存求 SHA-256（用于小数据与测试）
[[nodiscard]] std::string Sha256Hex(std::string_view data);

struct FileDigest {
  std::string sha256_hex;
  std::uint64_t size_bytes{0};
};

// 流式计算一个文件的 SHA-256 与大小。
// 用固定大小缓冲区循环读取，整个文件不会进内存（大文件合并后的校验走这里）。
[[nodiscard]] Result<FileDigest> Sha256OfFile(const std::string& path);

}  // namespace lan_file_hub
