#include "lan_file_hub/application/user_directory.h"

#include <algorithm>

namespace lan_file_hub {
namespace {

// 去掉首尾空白：用户可能带空格粘贴
std::string Trim(std::string_view text) {
  const auto begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string_view::npos) {
    return {};
  }
  const auto end = text.find_last_not_of(" \t\r\n");
  return std::string(text.substr(begin, end - begin + 1));
}

}  // namespace

Result<std::vector<UserSummary>> SearchUsersForSharing(UserRepository& users,
                                                       std::string_view keyword,
                                                       std::string_view exclude_user_id,
                                                       std::size_t limit) {
  const std::string trimmed = Trim(keyword);
  if (trimmed.size() < kMinUserQueryLength) {
    return Error{"INVALID_ARGUMENT", "请输入至少 2 个字符的用户名或邮箱"};
  }

  const std::size_t capped =
      std::min(limit == 0 ? kMaxUserQueryResults : limit, kMaxUserQueryResults);

  UserRepository::UserQuery query;
  query.search = trimmed;
  query.status = ToDbString(UserStatus::kActive);  // 只找还能登录的用户
  // 多取一条：下面要把"自己"过滤掉，避免刚好把自己算进上限里
  auto found = users.List(query, "username", false, static_cast<int>(capped) + 1, 0);
  if (!found) {
    return found.GetError();
  }

  std::vector<UserSummary> summaries;
  summaries.reserve(capped);
  for (const auto& user : found.Value()) {
    if (user.id == exclude_user_id) {
      continue;
    }
    summaries.push_back(UserSummary{user.id, user.username});
    if (summaries.size() >= capped) {
      break;
    }
  }
  return summaries;
}

}  // namespace lan_file_hub
