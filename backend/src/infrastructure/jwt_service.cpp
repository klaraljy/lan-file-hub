#include "lan_file_hub/infrastructure/jwt_service.h"

#include <chrono>
#include <optional>
#include <system_error>
#include <utility>

#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/defaults.h>

namespace lan_file_hub {
namespace {

constexpr char kClaimUsername[] = "username";
constexpr char kClaimRole[] = "role";

// jwt-cpp 0.7 起必须显式选择 JSON traits；这里用 nlohmann-json（项目已定选型）
using JsonTraits = jwt::traits::nlohmann_json;
using DecodedJwt = jwt::decoded_jwt<JsonTraits>;

std::int64_t ToUnix(const std::chrono::system_clock::time_point& point) {
  return std::chrono::duration_cast<std::chrono::seconds>(point.time_since_epoch()).count();
}

}  // namespace

JwtService::JwtService(JwtOptions options) : options_(std::move(options)) {}

Result<std::string> JwtService::Issue(const User& user) const {
  if (options_.secret.empty()) {
    return Error{"INTERNAL_ERROR", "JWT 密钥未配置"};
  }

  const auto now = std::chrono::system_clock::now();
  try {
    const std::string token =
        jwt::create()
            .set_issuer(options_.issuer)
            .set_type("JWT")
            .set_subject(user.id)
            .set_issued_at(now)
            .set_expires_at(now + std::chrono::seconds(options_.expire_seconds))
            .set_payload_claim(kClaimUsername, jwt::claim(user.username))
            .set_payload_claim(kClaimRole, jwt::claim(ToDbString(user.role)))
            .sign(jwt::algorithm::hs256{options_.secret});
    return token;
  } catch (const std::exception& ex) {
    return Error{"INTERNAL_ERROR", "签发 Token 失败", ex.what()};
  }
}

Result<JwtClaims> JwtService::Verify(std::string_view token) const {
  if (options_.secret.empty()) {
    return Error{"INTERNAL_ERROR", "JWT 密钥未配置"};
  }
  if (token.empty()) {
    return Error{"UNAUTHENTICATED", "缺少访问令牌"};
  }

  // 注意：decoded_jwt 的构造函数本身就会解码，构造失败会抛异常，
  // 所以不能在 try 之外先构造一个「空」对象，改用 optional 承接。
  std::optional<DecodedJwt> decoded_holder;
  try {
    decoded_holder = jwt::decode(std::string(token));
  } catch (const std::exception&) {
    // 不回显 token 内容，避免把凭据写进日志
    return Error{"UNAUTHENTICATED", "访问令牌格式无效"};
  }
  const DecodedJwt& decoded = *decoded_holder;

  // 用 error_code 重载而不是异常：这样能精确区分「过期」与「无效」，
  // 从而按要求返回 TOKEN_EXPIRED（docs/phases/02-auth.md 安全要求）。
  std::error_code ec;
  try {
    jwt::verify()
        .allow_algorithm(jwt::algorithm::hs256{options_.secret})
        .with_issuer(options_.issuer)
        .verify(decoded, ec);
  } catch (const std::exception&) {
    return Error{"UNAUTHENTICATED", "访问令牌校验失败"};
  }
  if (ec) {
    if (ec == jwt::error::token_verification_error::token_expired) {
      return Error{"TOKEN_EXPIRED", "访问令牌已过期"};
    }
    return Error{"UNAUTHENTICATED", "访问令牌校验失败"};
  }

  try {
    if (!decoded.has_expires_at()) {
      return Error{"UNAUTHENTICATED", "访问令牌缺少过期时间"};
    }
    // 兜底：即便库没有启用内建 exp 校验，这里也能挡住过期令牌
    const auto expires_at = decoded.get_expires_at();
    if (ToUnix(expires_at) <= ToUnix(std::chrono::system_clock::now())) {
      return Error{"TOKEN_EXPIRED", "访问令牌已过期"};
    }

    JwtClaims claims;
    claims.user_id = decoded.get_subject();
    claims.username = decoded.get_payload_claim(kClaimUsername).as_string();
    claims.role = ParseUserRole(decoded.get_payload_claim(kClaimRole).as_string());
    claims.expires_at_unix = ToUnix(expires_at);
    if (claims.user_id.empty()) {
      return Error{"UNAUTHENTICATED", "访问令牌缺少用户标识"};
    }
    return claims;
  } catch (const std::exception&) {
    return Error{"UNAUTHENTICATED", "访问令牌声明不完整"};
  }
}

}  // namespace lan_file_hub
