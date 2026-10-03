// 邀请的纯逻辑单元测试（不连数据库）：可用性判定、剩余次数、token 哈希口径。
// 用户 2026-10-02 要求「不是所有人都能注册，要通过管理员邀请」，这条边界必须有测试守着。
#include <string>

#include <gtest/gtest.h>

#include "lan_file_hub/domain/invite.h"
#include "lan_file_hub/infrastructure/invite_repository.h"

namespace lan_file_hub {
namespace {

Invite MakeInvite() {
  Invite invite;
  invite.id = "invite-1";
  invite.note = "给小王";
  invite.max_uses = 1;
  invite.used_count = 0;
  return invite;
}

TEST(InviteTest, FreshInviteIsUsable) {
  const Invite invite = MakeInvite();
  EXPECT_TRUE(invite.Usable(1'000));
  EXPECT_EQ(invite.RemainingUses(), 1);
}

TEST(InviteTest, ExhaustedInviteIsNotUsable) {
  Invite invite = MakeInvite();
  invite.used_count = invite.max_uses;
  EXPECT_FALSE(invite.Usable(1'000)) << "次数用完的邀请不能再注册";
  EXPECT_EQ(invite.RemainingUses(), 0);
}

TEST(InviteTest, MultiUseInviteCountsDown) {
  Invite invite = MakeInvite();
  invite.max_uses = 3;
  invite.used_count = 1;
  EXPECT_TRUE(invite.Usable(1'000));
  EXPECT_EQ(invite.RemainingUses(), 2);
}

TEST(InviteTest, RevokedInviteIsNotUsable) {
  Invite invite = MakeInvite();
  invite.revoked_at = "2026-10-02T00:00:00Z";
  EXPECT_FALSE(invite.Usable(1'000)) << "撤销后即使还有次数也不能用";
}

TEST(InviteTest, ExpiredInviteIsNotUsable) {
  Invite invite = MakeInvite();
  invite.expires_at_unix = 900;
  invite.expires_at = "2026-10-02T00:15:00Z";
  EXPECT_FALSE(invite.Usable(1'000)) << "过期时间早于当前时间即失效";
  EXPECT_TRUE(invite.Usable(800)) << "到期前仍然可用";
  EXPECT_FALSE(invite.Usable(900)) << "刚好到期即失效（边界取闭区间外侧）";
}

TEST(InviteTest, WithoutExpiryStaysUsable) {
  const Invite invite = MakeInvite();
  EXPECT_FALSE(invite.expires_at_unix.has_value());
  EXPECT_TRUE(invite.Usable(2'000'000'000));
}

TEST(InviteTest, TokenHashIsStableAndHidesPlaintext) {
  const std::string token = "abcDEF123-_token";
  const std::string hash = HashInviteToken(token);
  EXPECT_EQ(hash, HashInviteToken(token)) << "同一 token 必须得到同一哈希（查库要用）";
  EXPECT_EQ(hash.size(), 64U) << "SHA-256 十六进制是 64 位";
  EXPECT_EQ(hash.find(token), std::string::npos) << "哈希里不能出现明文";
  EXPECT_NE(hash, HashInviteToken(token + "x")) << "不同 token 必须不同哈希";
}

TEST(InviteTest, GeneratedTokensAreRandomAndUrlSafe) {
  auto first = GenerateInviteToken();
  auto second = GenerateInviteToken();
  ASSERT_TRUE(first.Ok()) << first.GetError().message;
  ASSERT_TRUE(second.Ok()) << second.GetError().message;
  EXPECT_NE(first.Value(), second.Value()) << "两次生成必须不同（256 位随机）";
  EXPECT_EQ(first.Value().size(), 43U) << "32 字节 base64url = 43 个字符";
  for (const char ch : first.Value()) {
    const bool url_safe = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                          (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
    EXPECT_TRUE(url_safe) << "token 必须能直接放进链接，非法字符: " << ch;
  }
}

}  // namespace
}  // namespace lan_file_hub
