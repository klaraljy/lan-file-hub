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
// purpose 用来区分「会话令牌」与「下载票据」：两者必须互不通用
constexpr char kClaimPurpose[] = "purpose";
constexpr char kPurposeSession[] = "session";
constexpr char kPurposeDownload[] = "download";
constexpr char kClaimFileId[] = "fid";
constexpr char kClaimVersionId[] = "vid";

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
            .set_payload_claim(kClaimPurpose, jwt::claim(std::string(kPurposeSession)))
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

    // 下载票据不能当会话令牌用（有效期只有 60 秒，但语义必须分清）
    if (decoded.has_payload_claim(kClaimPurpose)) {
      const std::string purpose = decoded.get_payload_claim(kClaimPurpose).as_string();
      if (purpose != kPurposeSession) {
        return Error{"UNAUTHENTICATED", "该令牌不能用于访问接口"};
      }
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

Result<std::string> JwtService::IssueDownloadTicket(std::string_view user_id,
                                                    std::string_view file_id,
                                                    std::string_view version_id,
                                                    std::uint32_t ttl_seconds) const {
  if (options_.secret.empty()) {
    return Error{"INTERNAL_ERROR", "JWT 密钥未配置"};
  }
  if (user_id.empty() || file_id.empty()) {
    return Error{"INTERNAL_ERROR", "签发下载票据失败：缺少用户或文件标识"};
  }

  const auto now = std::chrono::system_clock::now();
  const auto ttl = std::chrono::seconds(ttl_seconds == 0 ? kDownloadTicketTtlSeconds : ttl_seconds);
  try {
    const std::string ticket =
        jwt::create()
            .set_issuer(options_.issuer)
            .set_type("JWT")
            .set_subject(std::string(user_id))
            .set_issued_at(now)
            .set_expires_at(now + ttl)
            .set_payload_claim(kClaimPurpose, jwt::claim(std::string(kPurposeDownload)))
            .set_payload_claim(kClaimFileId, jwt::claim(std::string(file_id)))
            .set_payload_claim(kClaimVersionId, jwt::claim(std::string(version_id)))
            .sign(jwt::algorithm::hs256{options_.secret});
    return ticket;
  } catch (const std::exception& ex) {
    return Error{"INTERNAL_ERROR", "签发下载票据失败", ex.what()};
  }
}

Result<DownloadTicketClaims> JwtService::VerifyDownloadTicket(std::string_view token) const {
  if (options_.secret.empty()) {
    return Error{"INTERNAL_ERROR", "JWT 密钥未配置"};
  }
  if (token.empty()) {
    return Error{"UNAUTHENTICATED", "缺少下载票据"};
  }

  std::optional<DecodedJwt> decoded_holder;
  try {
    decoded_holder = jwt::decode(std::string(token));
  } catch (const std::exception&) {
    return Error{"UNAUTHENTICATED", "下载票据格式无效"};
  }
  const DecodedJwt& decoded = *decoded_holder;

  std::error_code ec;
  try {
    jwt::verify()
        .allow_algorithm(jwt::algorithm::hs256{options_.secret})
        .with_issuer(options_.issuer)
        .verify(decoded, ec);
  } catch (const std::exception&) {
    return Error{"UNAUTHENTICATED", "下载票据校验失败"};
  }
  if (ec) {
    if (ec == jwt::error::token_verification_error::token_expired) {
      return Error{"TOKEN_EXPIRED", "下载链接已过期，请回到页面重新点击下载"};
    }
    return Error{"UNAUTHENTICATED", "下载票据校验失败"};
  }

  try {
    if (!decoded.has_expires_at()) {
      return Error{"UNAUTHENTICATED", "下载票据缺少过期时间"};
    }
    const std::string purpose = decoded.has_payload_claim(kClaimPurpose)
                                    ? decoded.get_payload_claim(kClaimPurpose).as_string()
                                    : std::string{};
    if (purpose != kPurposeDownload) {
      // 会话令牌不能当票据用（否则等于把长期凭据写进 URL）
      return Error{"UNAUTHENTICATED", "这不是下载票据"};
    }

    DownloadTicketClaims claims;
    claims.user_id = decoded.get_subject();
    claims.file_id = decoded.get_payload_claim(kClaimFileId).as_string();
    if (decoded.has_payload_claim(kClaimVersionId)) {
      claims.version_id = decoded.get_payload_claim(kClaimVersionId).as_string();
    }
    claims.expires_at_unix = ToUnix(decoded.get_expires_at());
    if (claims.user_id.empty() || claims.file_id.empty()) {
      return Error{"UNAUTHENTICATED", "下载票据声明不完整"};
    }
    return claims;
  } catch (const std::exception&) {
    return Error{"UNAUTHENTICATED", "下载票据声明不完整"};
  }
}

}  // namespace lan_file_hub
