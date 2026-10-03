#include "lan_file_hub/application/invite_service.h"

#include <chrono>
#include <string>

namespace lan_file_hub {
namespace {

std::int64_t NowUnix() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// 邀请不可用时的统一错误：过期 / 用完 / 已撤销都归这一类，不告诉外部具体原因
// （否则等于帮人枚举"哪条邀请还存在"）。
Error UnusableInvite() {
  return Error{"INVITE_INVALID", "邀请链接无效、已过期或已用完，请让管理员重新发一条"};
}

}  // namespace

Result<InviteService::CreatedInvite> InviteService::Create(const CreateRequest& request,
                                                           std::string_view admin_user_id) {
  if (request.note.size() > kMaxNoteLength) {
    return Error{"INVALID_ARGUMENT", "备注过长（最多 200 字）"};
  }
  if (request.max_uses < 1 || request.max_uses > kMaxUsesLimit) {
    return Error{"INVALID_ARGUMENT", "可用次数必须在 1 到 1000 之间"};
  }
  if (request.expires_in_seconds.has_value()) {
    const std::int64_t seconds = *request.expires_in_seconds;
    if (seconds < kMinExpiresSeconds || seconds > kMaxExpiresSeconds) {
      return Error{"INVALID_ARGUMENT", "有效期必须是 5 分钟到 1 年之间"};
    }
  }

  auto token = GenerateInviteToken();
  if (!token) {
    return token.GetError();
  }

  std::optional<std::int64_t> expires_at;
  if (request.expires_in_seconds.has_value()) {
    expires_at = NowUnix() + *request.expires_in_seconds;
  }

  auto created = repository_.Create(HashInviteToken(token.Value()), request.note, admin_user_id,
                                    expires_at, request.max_uses);
  if (!created) {
    return created.GetError();
  }

  CreatedInvite result;
  result.invite = created.Value();
  result.token = token.Value();
  return result;
}

Result<std::vector<Invite>> InviteService::List(int limit, int offset) {
  if (limit < 1 || limit > 100) {
    return Error{"INVALID_ARGUMENT", "page_size 必须在 1 到 100 之间"};
  }
  if (offset < 0) {
    return Error{"INVALID_ARGUMENT", "offset 不能为负"};
  }
  return repository_.List(limit, offset);
}

Result<std::uint64_t> InviteService::Count() { return repository_.Count(); }

Status InviteService::Revoke(std::string_view invite_id) {
  if (invite_id.empty()) {
    return Status(Error{"INVALID_ARGUMENT", "缺少邀请 id"});
  }
  return repository_.Revoke(invite_id);
}

Result<Invite> InviteService::Describe(std::string_view token) {
  if (token.empty()) {
    return Error{"RESOURCE_NOT_FOUND", "邀请链接无效"};
  }
  auto found = repository_.FindByTokenHash(HashInviteToken(token));
  if (!found) {
    return found.GetError();
  }
  if (!found.Value().has_value()) {
    return Error{"RESOURCE_NOT_FOUND", "邀请链接无效"};
  }
  const Invite& invite = *found.Value();
  if (!invite.Usable(NowUnix())) {
    return UnusableInvite();
  }
  return invite;
}

Result<Invite> InviteService::Consume(std::string_view token) {
  if (token.empty()) {
    return Error{"INVITE_INVALID", "缺少邀请参数"};
  }
  const std::string token_hash = HashInviteToken(token);
  auto consumed = repository_.Consume(token_hash);
  if (!consumed) {
    return consumed.GetError();
  }
  if (!consumed.Value()) {
    return UnusableInvite();
  }
  // 消耗成功后把这条邀请读回来（写审计日志与响应要用它的 id/note）
  auto found = repository_.FindByTokenHash(token_hash);
  if (!found) {
    return found.GetError();
  }
  if (!found.Value().has_value()) {
    return Error{"INTERNAL_ERROR", "邀请状态异常"};
  }
  return *found.Value();
}

Status InviteService::Release(std::string_view token) {
  if (token.empty()) {
    return Status::Success();
  }
  return repository_.Release(HashInviteToken(token));
}

}  // namespace lan_file_hub
