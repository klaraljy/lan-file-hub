#include "lan_file_hub/infrastructure/password_hasher.h"

#include <array>
#include <cstring>
#include <random>
#include <vector>

#include <argon2.h>

namespace lan_file_hub {
namespace {

std::vector<std::uint8_t> RandomSalt(std::uint32_t bytes) {
  static thread_local std::mt19937_64 engine(std::random_device{}());
  std::uniform_int_distribution<unsigned int> dist(0, 255);

  std::vector<std::uint8_t> salt(bytes);
  for (auto& b : salt) {
    b = static_cast<std::uint8_t>(dist(engine));
  }
  return salt;
}

}  // namespace

PasswordHasher::PasswordHasher(Argon2Options options) : options_(options) {}

Result<std::string> PasswordHasher::Hash(std::string_view password) const {
  if (password.size() < kMinPasswordBytes) {
    return Error{"INVALID_ARGUMENT", "密码长度不足",
                 "至少 " + std::to_string(kMinPasswordBytes) + " 个字节"};
  }
  if (password.size() > kMaxPasswordBytes) {
    return Error{"INVALID_ARGUMENT", "密码过长",
                 "最多 " + std::to_string(kMaxPasswordBytes) + " 个字节"};
  }

  const std::vector<std::uint8_t> salt = RandomSalt(options_.salt_bytes);

  // 编码串长度上界：算法名 + 参数 + base64(salt) + base64(hash) 足够放下
  std::vector<char> encoded(
      argon2_encodedlen(options_.time_cost, options_.memory_cost_kib, options_.parallelism,
                        static_cast<uint32_t>(salt.size()), options_.hash_bytes, Argon2_id));

  const int rc =
      argon2id_hash_encoded(options_.time_cost, options_.memory_cost_kib, options_.parallelism,
                            password.data(), password.size(), salt.data(), salt.size(),
                            options_.hash_bytes, encoded.data(), encoded.size());
  if (rc != ARGON2_OK) {
    // argon2 的错误串不含口令，可以安全记录
    return Error{"INTERNAL_ERROR", "密码哈希失败", argon2_error_message(rc)};
  }
  return std::string(encoded.data());
}

bool PasswordHasher::Verify(std::string_view password, std::string_view encoded_hash) const {
  if (password.empty() || encoded_hash.empty() || password.size() > kMaxPasswordBytes) {
    return false;
  }
  // argon2id_verify 需要 NUL 结尾的编码串
  const std::string encoded(encoded_hash);
  return argon2id_verify(encoded.c_str(), password.data(), password.size()) == ARGON2_OK;
}

}  // namespace lan_file_hub
