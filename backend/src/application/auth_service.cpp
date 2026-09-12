#include "lan_file_hub/application/auth_service.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include <spdlog/spdlog.h>

#include "lan_file_hub/common/uuid.h"

namespace lan_file_hub {
namespace {

// 一次「假」哈希校验，用于用户不存在时消耗与真实校验相当的时间，
// 避免通过响应时间判断账号是否存在。
constexpr char kDummyHash[] =
    "$argon2id$v=19$m=65536,t=3,p=1$c2FsdHNhbHRzYWx0c2E$"
    "5P3v0S3nGk8kQW7oQ0mQ0mQ0mQ0mQ0mQ0mQ0mQ0mQ0";

bool IsAllowedUsernameChar(unsigned char c) {
  if (c >= '0' && c <= '9')
    return true;
  if (c >= 'a' && c <= 'z')
    return true;
  if (c >= 'A' && c <= 'Z')
    return true;
  return c == '.' || c == '_' || c == '-';
}

bool LooksLikeEmail(std::string_view email) {
  const auto at = email.find('@');
  if (at == std::string_view::npos || at == 0 || at + 1 >= email.size())
    return false;
  if (email.find(' ') != std::string_view::npos)
    return false;
  const auto dot = email.find('.', at);
  return dot != std::string_view::npos && dot + 1 < email.size();
}

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

}  // namespace

AuthService::AuthService(UserRepository& users, const PasswordHasher& hasher, const JwtService& jwt,
                         AuthServiceOptions options, FolderRepository* folders)
    : users_(users), hasher_(hasher), jwt_(jwt), options_(options), folders_(folders) {}

Result<bool> AuthService::ValidateUsername(std::string_view username) {
  if (username.size() < 3 || username.size() > 64) {
    return Error{"INVALID_ARGUMENT", "用户名长度必须在 3 到 64 之间"};
  }
  for (const char raw : username) {
    const auto c = static_cast<unsigned char>(raw);
    if (c > 0x7F || !IsAllowedUsernameChar(c)) {
      return Error{"INVALID_ARGUMENT", "用户名只能包含字母、数字、点、下划线和短横线"};
    }
  }
  return true;
}

Result<bool> AuthService::ValidateEmail(std::string_view email) {
  if (email.empty() || email.size() > 254) {
    return Error{"INVALID_ARGUMENT", "邮箱长度必须在 1 到 254 之间"};
  }
  if (!LooksLikeEmail(email)) {
    return Error{"INVALID_ARGUMENT", "邮箱格式不正确"};
  }
  return true;
}

Result<AuthResult> AuthService::IssueFor(const User& user) const {
  auto token = jwt_.Issue(user);
  if (!token) {
    return token.GetError();
  }
  AuthResult result;
  result.access_token = token.Value();
  result.expires_in_seconds = jwt_.ExpiresInSeconds();
  result.user = user;
  return result;
}

Result<AuthResult> AuthService::Register(const RegisterRequest& request) {
  auto username_ok = ValidateUsername(request.username);
  if (!username_ok)
    return username_ok.GetError();
  auto email_ok = ValidateEmail(request.email);
  if (!email_ok)
    return email_ok.GetError();

  auto hashed = hasher_.Hash(request.password);
  if (!hashed) {
    return hashed.GetError();
  }

  User user;
  user.id = GenerateUuidV4();
  user.username = request.username;
  user.email = ToLower(request.email);
  user.password_hash = hashed.Value();
  user.role = UserRole::kUser;  // 注册一律是普通用户，管理员由运维提升
  user.status = UserStatus::kActive;
  user.storage_quota_bytes = options_.default_storage_quota_bytes;
  user.used_storage_bytes = 0;

  auto created = users_.Create(user);
  if (!created) {
    // RESOURCE_CONFLICT 直接透传，让前端提示「用户名或邮箱已被占用」
    return created.GetError();
  }

  // 预置用户根目录。失败不阻塞注册：EnsureRootFolder 在首次访问时还会再试一次。
  if (folders_ != nullptr) {
    auto root = folders_->EnsureRoot(created.Value().id);
    if (!root) {
      spdlog::error("注册后创建根目录失败（将在首次访问时重试）| user_id={} | code={}",
                    created.Value().id, root.GetError().code);
    }
  }

  return IssueFor(created.Value());
}

Result<AuthResult> AuthService::Login(const LoginRequest& request) {
  if (request.identifier.empty() || request.password.empty()) {
    return Error{"INVALID_ARGUMENT", "缺少用户名或密码"};
  }

  // 无论用户是否存在，都执行一次哈希校验，避免时间侧信道泄露账号是否存在
  const auto found = users_.FindByUsernameOrEmail(request.identifier);
  if (!found) {
    return found.GetError();
  }
  if (!found.Value().has_value()) {
    (void)hasher_.Verify(request.password, kDummyHash);
    return Error{"UNAUTHENTICATED", "用户名或密码错误"};
  }

  const User user = *found.Value();
  if (!hasher_.Verify(request.password, user.password_hash)) {
    return Error{"UNAUTHENTICATED", "用户名或密码错误"};
  }

  // 先验证口令再检查状态：口令不对的人不该知道这个账号被禁用了
  if (!user.IsActive()) {
    return Error{"FORBIDDEN", "账号已被禁用"};
  }

  return IssueFor(user);
}

Result<User> AuthService::CurrentUser(std::string_view user_id) {
  auto found = users_.FindById(user_id);
  if (!found) {
    return found.GetError();
  }
  if (!found.Value().has_value()) {
    return Error{"UNAUTHENTICATED", "用户不存在"};
  }
  const User user = *found.Value();
  if (!user.IsActive()) {
    return Error{"FORBIDDEN", "账号已被禁用"};
  }
  return user;
}

Status AuthService::Logout(std::string_view user_id) {
  if (user_id.empty()) {
    return Status(Error{"UNAUTHENTICATED", "缺少用户标识"});
  }
  // 无状态 Token：服务端不做失效处理，仅由调用方记录审计日志
  return Status::Success();
}

}  // namespace lan_file_hub
