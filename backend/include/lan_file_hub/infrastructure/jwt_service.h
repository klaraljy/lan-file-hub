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

// 下载票据（五阶段改造 ①：大文件流式下载）。
//
// 为什么需要它：登录态下载原来走 axios `responseType: 'blob'`，**整个文件先进浏览器内存**
// ——5 GB 必崩。改成"先换一张 60 秒有效的票据，再把票据拼进 URL 让浏览器自己下载"，
// 浏览器会流式落盘，内存占用与文件大小无关。
//
// 安全边界（务必保持）：
//   - 票据**只能用于下载**：`Verify()` 会拒绝 purpose=download 的令牌，防止拿 60 秒票据当会话令牌；
//   - 票据里写死 file_id（必要时还有 version_id），换文件用不了；
//   - 有效期极短（默认 60 秒），且仍然会**再查一次权限**（票据不等于授权）。
inline constexpr std::uint32_t kDownloadTicketTtlSeconds = 60;

struct DownloadTicketClaims {
  std::string user_id;
  std::string file_id;
  std::string version_id;  // 空 = 当前版本
  std::int64_t expires_at_unix{0};
};

class JwtService {
 public:
  explicit JwtService(JwtOptions options);

  // 签发。失败（例如密钥为空）返回 Error。
  [[nodiscard]] Result<std::string> Issue(const User& user) const;

  // 签发一个**指定有效期**的会话令牌（用户 2026-10-03：登录页的「记住我」——
  // 勾了就发长效令牌，手机 App 不用每次重登；不勾就还是默认的短令牌）。
  // ttl_seconds = 0 时回退到 options_.expire_seconds。
  [[nodiscard]] Result<std::string> Issue(const User& user, std::uint32_t ttl_seconds) const;

  // 校验并解析。失败时区分两种错误码：
  //   TOKEN_EXPIRED —— 签名有效但已过期（客户端应重新登录）
  //   UNAUTHENTICATED —— 格式错误、签名不匹配、issuer 不符、缺少必要声明
  // 注意：**不接受下载票据**（purpose 不是 session 的一律拒绝）。
  [[nodiscard]] Result<JwtClaims> Verify(std::string_view token) const;

  // 签发下载票据（ttl_seconds 传 0 时用 kDownloadTicketTtlSeconds）。
  [[nodiscard]] Result<std::string> IssueDownloadTicket(std::string_view user_id,
                                                        std::string_view file_id,
                                                        std::string_view version_id,
                                                        std::uint32_t ttl_seconds = 0) const;

  // 校验下载票据；过期或不是票据都返回错误。
  [[nodiscard]] Result<DownloadTicketClaims> VerifyDownloadTicket(std::string_view token) const;

  [[nodiscard]] std::int64_t ExpiresInSeconds() const {
    return static_cast<std::int64_t>(options_.expire_seconds);
  }

 private:
  JwtOptions options_;
};

}  // namespace lan_file_hub
