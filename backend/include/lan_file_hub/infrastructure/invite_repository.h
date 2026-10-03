// 邀请仓储。所有 SQL 参数化（docs/security.md），不做字符串拼接。
//
// token 与分享链接同一套口径：256 位随机 + base64url，库里**只存 SHA-256**。
// 消耗用一条原子 UPDATE（`used_count < max_uses AND 未撤销 AND 未过期`）完成，
// 并发下不会把一条邀请用超。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lan_file_hub/common/result.h"
#include "lan_file_hub/domain/invite.h"
#include "lan_file_hub/infrastructure/database.h"

namespace lan_file_hub {

class InviteRepository {
 public:
  explicit InviteRepository(ConnectionPool& pool) : pool_(pool) {}

  // expires_at_unix 传 0 表示"不过期"
  [[nodiscard]] Result<Invite> Create(std::string_view token_hash, std::string_view note,
                                      std::string_view created_by,
                                      std::optional<std::int64_t> expires_at_unix,
                                      std::int32_t max_uses);

  [[nodiscard]] Result<std::optional<Invite>> FindByTokenHash(std::string_view token_hash);

  [[nodiscard]] Result<std::vector<Invite>> List(int limit, int offset);
  [[nodiscard]] Result<std::uint64_t> Count();

  // 原子消耗一次。返回 true = 消耗成功（这条邀请此刻确实可用）；false = 不可用（撤销/过期/用完）
  [[nodiscard]] Result<bool> Consume(std::string_view token_hash);

  // 补偿：注册失败时把刚消耗的那一次还回去（只用于"已消耗但建号失败"这条路径）
  [[nodiscard]] Status Release(std::string_view token_hash);

  // 撤销（幂等：已撤销或不存在都算成功）
  [[nodiscard]] Status Revoke(std::string_view invite_id);

  // 仅供测试使用：清空 invites 表
  [[nodiscard]] Status DeleteAllForTesting();

 private:
  ConnectionPool& pool_;
};

// 邀请 token：与分享链接同口径（256 位随机、base64url、库里只存 SHA-256）
[[nodiscard]] Result<std::string> GenerateInviteToken();
[[nodiscard]] std::string HashInviteToken(std::string_view token);

}  // namespace lan_file_hub
