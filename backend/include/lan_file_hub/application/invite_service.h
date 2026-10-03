// 邀请业务规则（用户 2026-10-02：不是所有人都能注册，要通过管理员邀请）。
//
// 分层：Controller 只做参数与响应，规则在这里；SQL 在 InviteRepository。
// 关键约束：**token 明文只在 Create 的返回值里出现一次**，之后任何接口都拿不到（库里只有哈希）。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lan_file_hub/common/result.h"
#include "lan_file_hub/domain/invite.h"
#include "lan_file_hub/infrastructure/invite_repository.h"

namespace lan_file_hub {

class InviteService {
 public:
  explicit InviteService(InviteRepository& repository) : repository_(repository) {}

  struct CreateRequest {
    std::string note;
    std::optional<std::int64_t> expires_in_seconds;  // 省略 = 不过期
    std::int32_t max_uses{1};
  };

  struct CreatedInvite {
    Invite invite;
    std::string token;  // ⛔ 明文只在这里返回一次
  };

  [[nodiscard]] Result<CreatedInvite> Create(const CreateRequest& request,
                                             std::string_view admin_user_id);
  [[nodiscard]] Result<std::vector<Invite>> List(int limit, int offset);
  [[nodiscard]] Result<std::uint64_t> Count();
  [[nodiscard]] Status Revoke(std::string_view invite_id);

  // 公开校验（注册页拿 token 问"这条邀请还能用吗"）
  [[nodiscard]] Result<Invite> Describe(std::string_view token);
  // 原子消耗一次（注册之前调用；注册失败必须 Release 还回去，否则白白吃掉一次）
  [[nodiscard]] Result<Invite> Consume(std::string_view token);
  [[nodiscard]] Status Release(std::string_view token);

  // 防呆上限（同时是接口校验规则，测试直接引用）
  static constexpr std::int32_t kMaxUsesLimit = 1000;
  static constexpr std::int64_t kMinExpiresSeconds = 300;             // 5 分钟
  static constexpr std::int64_t kMaxExpiresSeconds = 365LL * 24 * 3600;  // 1 年
  static constexpr std::size_t kMaxNoteLength = 200;

 private:
  InviteRepository& repository_;
};

}  // namespace lan_file_hub
