// 用户领域模型。字段与 docs/data-model.md §User 一一对应。
#pragma once

#include <cstdint>
#include <string>

namespace lan_file_hub {

enum class UserRole { kUser, kAdmin };

enum class UserStatus { kActive, kDisabled };

// 数据库里存的是大写字符串（见 migrations/0001_create_users.sql）
[[nodiscard]] std::string ToDbString(UserRole role);
[[nodiscard]] std::string ToDbString(UserStatus status);
[[nodiscard]] UserRole ParseUserRole(const std::string& value);
[[nodiscard]] UserStatus ParseUserStatus(const std::string& value);

struct User {
  std::string id;  // UUID
  std::string username;
  std::string email;
  std::string password_hash;
  UserRole role{UserRole::kUser};
  UserStatus status{UserStatus::kActive};
  std::uint64_t storage_quota_bytes{0};
  std::uint64_t used_storage_bytes{0};
  // 是否让别人看到自己在在线列表里（五阶段改造 ⑤：广场）。
  // 默认可见：用户 2026-09-21 明确要求"只要在线就默认显示，可以在设置里隐藏"。
  bool presence_visible{true};
  std::string created_at;  // ISO8601 字符串
  std::string updated_at;

  [[nodiscard]] bool IsActive() const { return status == UserStatus::kActive; }
  [[nodiscard]] bool IsAdmin() const { return role == UserRole::kAdmin; }
};

}  // namespace lan_file_hub
