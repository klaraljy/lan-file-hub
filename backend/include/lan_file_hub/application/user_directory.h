// 用户名录：按用户名或邮箱"找人"，用于把文件发送给某个人（用户 2026-09-21 指示，AGENTS.md §14.3）。
//
// 为什么需要它：给"某个人"传文件时，界面原来要求填用户 UUID，而界面上任何地方都查不到 UUID
// → 等于没法按人分享（用户真机实测踩到的缺陷）。这里提供"输入名字或邮箱就能选人"的服务端能力。
//
// 安全边界（按 docs/security.md 的最小暴露原则）：
//   - 只返回 id 与 username，**不返回邮箱、角色、配额等任何其它字段**；
//   - 排除调用者自己与非 ACTIVE 用户；
//   - 查询串有长度下限、结果条数有上限（防止拿它枚举全站用户）。
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "lan_file_hub/common/result.h"
#include "lan_file_hub/infrastructure/user_repository.h"

namespace lan_file_hub {

// 名录里给前端显示的最小信息
struct UserSummary {
  std::string id;
  std::string username;
};

// 查询串下限（太短就等于遍历用户表）
inline constexpr std::size_t kMinUserQueryLength = 2;
// 单次返回条数上限
inline constexpr std::size_t kMaxUserQueryResults = 10;

// 按用户名或邮箱模糊找人（大小写不敏感），排除 exclude_user_id 自己与非 ACTIVE 用户。
// keyword 去掉首尾空白后不足 kMinUserQueryLength → 返回 INVALID_ARGUMENT。
[[nodiscard]] Result<std::vector<UserSummary>> SearchUsersForSharing(
    UserRepository& users, std::string_view keyword, std::string_view exclude_user_id,
    std::size_t limit = kMaxUserQueryResults);

}  // namespace lan_file_hub
