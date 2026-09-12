// 认证应用服务：注册、登录、查询当前用户。
//
// 安全要求（docs/phases/02-auth.md）：
//   - 登录失败一律返回通用错误，不泄露用户是否存在
//   - 被禁用用户不得登录，也不得访问受保护接口
//   - 不在日志里输出密码、Token 或密码哈希
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "lan_file_hub/common/result.h"
#include "lan_file_hub/domain/user.h"
#include "lan_file_hub/infrastructure/folder_repository.h"
#include "lan_file_hub/infrastructure/jwt_service.h"
#include "lan_file_hub/infrastructure/password_hasher.h"
#include "lan_file_hub/infrastructure/user_repository.h"

namespace lan_file_hub {

struct RegisterRequest {
  std::string username;
  std::string email;
  std::string password;
};

struct LoginRequest {
  std::string identifier;  // 用户名或邮箱
  std::string password;
};

struct AuthResult {
  std::string access_token;
  std::int64_t expires_in_seconds{0};
  User user;
};

struct AuthServiceOptions {
  std::uint64_t default_storage_quota_bytes{20ULL * 1024 * 1024 * 1024};
};

class AuthService {
 public:
  // folders 可以为 nullptr：注册后不预置根目录（测试用），
  // 生产装配会传入，FolderService::EnsureRootFolder 也会兜底自愈。
  AuthService(UserRepository& users, const PasswordHasher& hasher, const JwtService& jwt,
              AuthServiceOptions options = {}, FolderRepository* folders = nullptr);

  [[nodiscard]] Result<AuthResult> Register(const RegisterRequest& request);

  [[nodiscard]] Result<AuthResult> Login(const LoginRequest& request);

  // 读取当前用户；顺带复查状态，被禁用则拒绝（不能只依赖 Token 是否过期）
  [[nodiscard]] Result<User> CurrentUser(std::string_view user_id);

  // JWT 是无状态的：登出由客户端丢弃 Token 完成，服务端只做审计留痕。
  // 引入 Token 黑名单需要 Redis，属于阶段十二的范围。
  [[nodiscard]] Status Logout(std::string_view user_id);

  // 供测试与校验复用的纯函数
  [[nodiscard]] static Result<bool> ValidateUsername(std::string_view username);
  [[nodiscard]] static Result<bool> ValidateEmail(std::string_view email);

 private:
  [[nodiscard]] Result<AuthResult> IssueFor(const User& user) const;

  UserRepository& users_;
  const PasswordHasher& hasher_;
  const JwtService& jwt_;
  AuthServiceOptions options_;
  FolderRepository* folders_{nullptr};
};

}  // namespace lan_file_hub
