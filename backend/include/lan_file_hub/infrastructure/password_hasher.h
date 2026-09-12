// 密码哈希：Argon2id（AGENTS.md §2 已定选型）。
// 绝不保存明文；不得把明文或哈希写进日志（docs/security.md）。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "lan_file_hub/common/result.h"

namespace lan_file_hub {

struct Argon2Options {
  std::uint32_t time_cost{3};            // 迭代次数
  std::uint32_t memory_cost_kib{65536};  // 64 MiB
  std::uint32_t parallelism{1};
  std::uint32_t salt_bytes{16};
  std::uint32_t hash_bytes{32};
};

// 密码长度上限：防止有人拿超长输入拖垮哈希计算（DoS）。
inline constexpr std::size_t kMaxPasswordBytes = 1024;
inline constexpr std::size_t kMinPasswordBytes = 8;

class PasswordHasher {
 public:
  explicit PasswordHasher(Argon2Options options = {});

  // 返回 PHC 格式的编码串：$argon2id$v=19$m=65536,t=3,p=1$<salt>$<hash>
  [[nodiscard]] Result<std::string> Hash(std::string_view password) const;

  // 校验；编码串损坏或算法不匹配时返回 false（不抛异常）。
  [[nodiscard]] bool Verify(std::string_view password, std::string_view encoded_hash) const;

  [[nodiscard]] const Argon2Options& options() const { return options_; }

 private:
  Argon2Options options_;
};

}  // namespace lan_file_hub
