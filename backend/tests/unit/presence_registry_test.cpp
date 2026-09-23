// 在线状态注册表的单元测试（五阶段改造 ⑤：广场 / 在线用户）。
//
// 这是纯内存逻辑，不需要数据库：覆盖心跳刷新、超时、隐身过滤、多设备、主动下线。
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "lan_file_hub/application/presence_registry.h"

namespace lan_file_hub {
namespace {

PresenceEntry MakeEntry(const std::string& user_id, const std::string& username,
                        const std::string& device_id, bool visible = true) {
  PresenceEntry entry;
  entry.user_id = user_id;
  entry.username = username;
  entry.device_id = device_id;
  entry.device_name = "Chrome · Windows";
  entry.platform = "web";
  entry.visible = visible;
  return entry;
}

bool HasUser(const std::vector<PresenceEntry>& entries, const std::string& user_id) {
  for (const auto& entry : entries) {
    if (entry.user_id == user_id) {
      return true;
    }
  }
  return false;
}

TEST(PresenceRegistryTest, TouchThenOnlineOthersSeesThem) {
  PresenceRegistry registry;
  registry.Touch(MakeEntry("u1", "alice", "dev-a"), 1000);
  registry.Touch(MakeEntry("u2", "bob", "dev-b"), 1000);

  const auto online = registry.OnlineOthers("u1", 1000);
  ASSERT_EQ(online.size(), 1U);
  EXPECT_EQ(online[0].user_id, "u2");
  EXPECT_EQ(online[0].username, "bob");
}

TEST(PresenceRegistryTest, NeverListsTheRequesterThemselves) {
  PresenceRegistry registry;
  registry.Touch(MakeEntry("u1", "alice", "dev-a"), 1000);
  EXPECT_TRUE(registry.OnlineOthers("u1", 1000).empty())
      << "广场里不该出现自己（自己看「我的设备」）";
}

TEST(PresenceRegistryTest, HiddenUserIsInvisibleToOthersButVisibleToSelf) {
  PresenceRegistry registry;
  registry.Touch(MakeEntry("u1", "alice", "dev-a"), 1000);
  registry.Touch(MakeEntry("u2", "bob", "dev-b", /*visible=*/false), 1000);

  const auto others = registry.OnlineOthers("u1", 1000);
  EXPECT_FALSE(HasUser(others, "u2")) << "隐身用户不能出现在别人的在线列表里";

  const auto devices = registry.DevicesOf("u2", 1000);
  ASSERT_EQ(devices.size(), 1U) << "自己始终能看到自己的设备";
  EXPECT_FALSE(devices[0].visible);
}

TEST(PresenceRegistryTest, SetVisibilityTakesEffectImmediately) {
  PresenceRegistry registry;
  registry.Touch(MakeEntry("u2", "bob", "dev-b"), 1000);
  ASSERT_EQ(registry.OnlineOthers("u1", 1000).size(), 1U);

  registry.SetVisibility("u2", false);
  EXPECT_TRUE(registry.OnlineOthers("u1", 1000).empty()) << "改成隐身后应立刻生效";

  registry.SetVisibility("u2", true);
  EXPECT_EQ(registry.OnlineOthers("u1", 1000).size(), 1U);
}

TEST(PresenceRegistryTest, EntryExpiresAfterTtl) {
  PresenceRegistry registry;
  registry.Touch(MakeEntry("u2", "bob", "dev-b"), 1000);
  EXPECT_EQ(registry.OnlineOthers("u1", 1000).size(), 1U);

  const std::int64_t just_inside = 1000 + kPresenceTtlSeconds;
  EXPECT_EQ(registry.OnlineOthers("u1", just_inside).size(), 1U) << "刚好在 TTL 边界内仍算在线";

  const std::int64_t past_ttl = 1000 + kPresenceTtlSeconds + 1;
  EXPECT_TRUE(registry.OnlineOthers("u1", past_ttl).empty()) << "超过 TTL 应算离线";
  EXPECT_TRUE(registry.DevicesOf("u2", past_ttl).empty());
}

TEST(PresenceRegistryTest, SweepRemovesOnlyStaleEntries) {
  PresenceRegistry registry;
  registry.Touch(MakeEntry("u1", "alice", "dev-a"), 1000);
  registry.Touch(MakeEntry("u2", "bob", "dev-b"), 2000);

  const std::size_t removed = registry.Sweep(2000);
  EXPECT_EQ(removed, 1U);
  EXPECT_EQ(registry.Size(), 1U);
  EXPECT_TRUE(HasUser(registry.DevicesOf("u2", 2000), "u2"));
}

TEST(PresenceRegistryTest, SameUserOnTwoDevicesShowsOnceForOthers) {
  PresenceRegistry registry;
  // 两台设备的心跳都在 TTL 窗口内（相差 50 秒 < 90 秒），否则旧的那台会先被判离线
  registry.Touch(MakeEntry("u2", "bob", "phone"), 1450);
  registry.Touch(MakeEntry("u2", "bob", "laptop"), 1500);

  const auto others = registry.OnlineOthers("u1", 1500);
  ASSERT_EQ(others.size(), 1U) << "同一个人多设备在线，广场里只显示一次";
  EXPECT_EQ(others[0].device_id, "laptop") << "显示最新活跃的那台";

  const auto devices = registry.DevicesOf("u2", 1500);
  EXPECT_EQ(devices.size(), 2U) << "自己的「我的设备」要列出全部";
  EXPECT_EQ(devices[0].device_id, "laptop") << "最新的排前面";
}

TEST(PresenceRegistryTest, RemoveDropsDeviceImmediately) {
  PresenceRegistry registry;
  registry.Touch(MakeEntry("u2", "bob", "dev-b"), 1000);
  registry.Remove("u2", "dev-b");
  EXPECT_TRUE(registry.OnlineOthers("u1", 1000).empty());
  EXPECT_EQ(registry.Size(), 0U);
}

TEST(PresenceRegistryTest, OnlineListSortedByMostRecent) {
  PresenceRegistry registry;
  // 三个人的心跳都要落在 TTL 窗口内，才能同时出现在在线列表里
  registry.Touch(MakeEntry("u2", "bob", "b"), 1160);
  registry.Touch(MakeEntry("u3", "carol", "c"), 1200);
  registry.Touch(MakeEntry("u4", "dave", "d"), 1180);

  const auto online = registry.OnlineOthers("u1", 1200);
  ASSERT_EQ(online.size(), 3U);
  EXPECT_EQ(online[0].username, "carol");
  EXPECT_EQ(online[1].username, "dave");
  EXPECT_EQ(online[2].username, "bob");
}

}  // namespace
}  // namespace lan_file_hub
