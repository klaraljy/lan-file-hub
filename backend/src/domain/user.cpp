#include "lan_file_hub/domain/user.h"

#include <stdexcept>

namespace lan_file_hub {

std::string ToDbString(UserRole role) {
  return role == UserRole::kAdmin ? "ADMIN" : "USER";
}

std::string ToDbString(UserStatus status) {
  return status == UserStatus::kDisabled ? "DISABLED" : "ACTIVE";
}

UserRole ParseUserRole(const std::string& value) {
  if (value == "ADMIN")
    return UserRole::kAdmin;
  if (value == "USER")
    return UserRole::kUser;
  throw std::invalid_argument("未知的用户角色: " + value);
}

UserStatus ParseUserStatus(const std::string& value) {
  if (value == "DISABLED")
    return UserStatus::kDisabled;
  if (value == "ACTIVE")
    return UserStatus::kActive;
  throw std::invalid_argument("未知的用户状态: " + value);
}

}  // namespace lan_file_hub
