// JWT 签发与校验（HS256）。
//
// 依据 docs/api.md §认证方案：本版采用**短期 JWT 单 Token** + 统一认证中间件。
// 密钥只能来自配置（环境变量），不得写入源码或日志。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "lan_file_hub/common/result.h"
#include "lan_file_hub/domain/user.h"

namespace lan_file_hub {

struct JwtOptions {
  std::string secret;  // 至少 32 字节；生产环境由 config 强制校验
  std::uint32_t expire_seconds{3600};
  std::string issuer{"lan-file-hub"};
};

struct JwtClaims {
  std::string user_id;
  std::string username;
  UserRole role{UserRole::kUser};
  std::int64_t expires_at_unix{0};
};

class JwtService {
 public:
  explicit JwtService(JwtOptions options);

  // 签发。失败（例如密钥为空）返回 Error。
  [[nodiscard]] Result<std::string> Issue(const User& user) const;

  // 校验并解析。失败时区分两种错误码：
  //   TOKEN_EXPIRED —— 签名有效但已过期（客户端应重新登录）
  //   UNAUTHENTICATED —— 格式错误、签名不匹配、issuer 不符、缺少必要声明
  [[nodiscard]] Result<JwtClaims> Verify(std::string_view token) const;

  [[nodiscard]] std::int64_t ExpiresInSeconds() const {
    return static_cast<std::int64_t>(options_.expire_seconds);
  }

 private:
  JwtOptions options_;
};

}  // namespace lan_file_hub
