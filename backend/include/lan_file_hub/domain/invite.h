// 邀请（注册用）。用户 2026-10-02 要求：不是所有人都能注册，要通过管理员邀请。
//
// ⛔ 这里**没有 token 明文字段**：库里只有 SHA-256（`token_hash`），
// 明文只在"创建邀请"的响应里返回一次 —— 与分享链接同一条安全口径（docs/security.md）。
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace lan_file_hub {

struct Invite {
  std::string id;
  std::string note;        // 备注：这条邀请是给谁的（管理员自己看）
  std::string created_by;  // 建这条邀请的管理员
  std::string created_at;  // ISO8601 UTC（与其它接口一致）
  std::optional<std::string> expires_at;         // ISO8601，输出用
  std::optional<std::int64_t> expires_at_unix;   // 判定用
  std::int32_t max_uses{1};
  std::int32_t used_count{0};
  std::optional<std::string> revoked_at;  // 非空 = 已撤销

  // 还能不能用。now_unix 由调用方传入，便于单测；真正的并发安全由 SQL 的原子 UPDATE 保证。
  [[nodiscard]] bool Usable(std::int64_t now_unix) const {
    if (revoked_at.has_value()) {
      return false;
    }
    if (used_count >= max_uses) {
      return false;
    }
    if (expires_at_unix.has_value() && *expires_at_unix <= now_unix) {
      return false;
    }
    return true;
  }

  [[nodiscard]] std::int32_t RemainingUses() const {
    return max_uses > used_count ? max_uses - used_count : 0;
  }
};

}  // namespace lan_file_hub
