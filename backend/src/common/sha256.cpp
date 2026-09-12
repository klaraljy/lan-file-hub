#include "lan_file_hub/common/sha256.h"

#include <array>
#include <fstream>
#include <memory>
#include <vector>

#include <openssl/evp.h>

namespace lan_file_hub {
namespace {

std::string ToHex(const unsigned char* bytes, unsigned int length) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(static_cast<std::size_t>(length) * 2);
  for (unsigned int i = 0; i < length; ++i) {
    out.push_back(kHex[bytes[i] >> 4]);
    out.push_back(kHex[bytes[i] & 0x0F]);
  }
  return out;
}

}  // namespace

Sha256Hasher::Sha256Hasher() {
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (ctx != nullptr && EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1) {
    ctx_ = ctx;
  } else if (ctx != nullptr) {
    EVP_MD_CTX_free(ctx);
  }
}

Sha256Hasher::~Sha256Hasher() {
  if (ctx_ != nullptr) {
    EVP_MD_CTX_free(static_cast<EVP_MD_CTX*>(ctx_));
  }
}

Status Sha256Hasher::Update(const char* data, std::size_t length) {
  if (ctx_ == nullptr) {
    return Status(Error{"INTERNAL_ERROR", "SHA-256 上下文未初始化"});
  }
  if (finished_) {
    return Status(Error{"INTERNAL_ERROR", "SHA-256 已经结束，不能继续追加"});
  }
  if (length == 0) {
    return Status::Success();
  }
  if (EVP_DigestUpdate(static_cast<EVP_MD_CTX*>(ctx_), data, length) != 1) {
    return Status(Error{"INTERNAL_ERROR", "SHA-256 更新失败"});
  }
  return Status::Success();
}

Result<std::string> Sha256Hasher::HexDigest() {
  if (ctx_ == nullptr) {
    return Error{"INTERNAL_ERROR", "SHA-256 上下文未初始化"};
  }
  if (finished_) {
    return Error{"INTERNAL_ERROR", "SHA-256 已经结束"};
  }
  finished_ = true;

  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int length = 0;
  if (EVP_DigestFinal_ex(static_cast<EVP_MD_CTX*>(ctx_), digest.data(), &length) != 1) {
    return Error{"INTERNAL_ERROR", "SHA-256 计算失败"};
  }
  return ToHex(digest.data(), length);
}

std::string Sha256Hex(std::string_view data) {
  Sha256Hasher hasher;
  if (!hasher.Update(data.data(), data.size()).Ok()) {
    return {};
  }
  auto digest = hasher.HexDigest();
  return digest.Ok() ? digest.Value() : std::string{};
}

Result<FileDigest> Sha256OfFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Error{"STORAGE_UNAVAILABLE", "无法打开待校验的文件"};
  }

  constexpr std::size_t kBufferBytes = 1U << 20;  // 1 MiB
  std::vector<char> buffer(kBufferBytes);
  Sha256Hasher hasher;
  FileDigest digest;

  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto read = input.gcount();
    if (read <= 0)
      break;
    auto updated = hasher.Update(buffer.data(), static_cast<std::size_t>(read));
    if (!updated.Ok()) {
      return updated.GetError();
    }
    digest.size_bytes += static_cast<std::uint64_t>(read);
  }
  if (input.bad()) {
    return Error{"STORAGE_UNAVAILABLE", "读取文件失败"};
  }

  auto hex = hasher.HexDigest();
  if (!hex.Ok()) {
    return hex.GetError();
  }
  digest.sha256_hex = hex.Value();
  return digest;
}

}  // namespace lan_file_hub
