// 限流器单元测试（阶段十二）。纯内存实现，不依赖数据库与网络。
#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "lan_file_hub/common/rate_limiter.h"

namespace lan_file_hub {
namespace {

TEST(RateLimiterTest, AllowsUpToLimitThenRejects) {
  RateLimiter limiter(3, std::chrono::seconds(60));
  EXPECT_TRUE(limiter.Acquire("k").allowed);
  EXPECT_TRUE(limiter.Acquire("k").allowed);
  const auto third = limiter.Acquire("k");
  EXPECT_TRUE(third.allowed);
  EXPECT_EQ(third.remaining, 0);

  const auto fourth = limiter.Acquire("k");
  EXPECT_FALSE(fourth.allowed) << "超过窗口上限必须被拒绝";
  EXPECT_GE(fourth.retry_after_seconds, 1) << "必须给出可用的 Retry-After";
}

TEST(RateLimiterTest, KeysAreIndependent) {
  RateLimiter limiter(2, std::chrono::seconds(60));
  EXPECT_TRUE(limiter.Acquire("a").allowed);
  EXPECT_TRUE(limiter.Acquire("a").allowed);
  EXPECT_FALSE(limiter.Acquire("a").allowed);
  EXPECT_TRUE(limiter.Acquire("b").allowed) << "一个 key 被限流不能影响其他 key";
}

TEST(RateLimiterTest, WindowExpiryRestoresQuota) {
  RateLimiter limiter(2, std::chrono::seconds(1));
  EXPECT_TRUE(limiter.Acquire("k").allowed);
  EXPECT_TRUE(limiter.Acquire("k").allowed);
  EXPECT_FALSE(limiter.Acquire("k").allowed);

  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  EXPECT_TRUE(limiter.Acquire("k").allowed) << "窗口过期后必须恢复额度";
}

TEST(RateLimiterTest, RejectedAttemptsDoNotExtendTheBlock) {
  // 关键行为：被拒绝的请求**不记账**。否则攻击者持续轰炸会把窗口一直顶住，
  // 限流就从"等待若干秒"退化成"永久封禁"，属于把安全措施做成了可用性事故。
  RateLimiter limiter(1, std::chrono::seconds(1));
  ASSERT_TRUE(limiter.Acquire("k").allowed);
  for (int i = 0; i < 20; ++i) {
    EXPECT_FALSE(limiter.Acquire("k").allowed);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  EXPECT_TRUE(limiter.Acquire("k").allowed);
}

TEST(RateLimiterTest, ResetClearsCounter) {
  RateLimiter limiter(1, std::chrono::seconds(60));
  ASSERT_TRUE(limiter.Acquire("k").allowed);
  ASSERT_FALSE(limiter.Acquire("k").allowed);
  limiter.Reset("k");
  EXPECT_TRUE(limiter.Acquire("k").allowed) << "登录成功后应能清空失败计数";
}

TEST(RateLimiterTest, PeekDoesNotConsumeQuota) {
  RateLimiter limiter(2, std::chrono::seconds(60));
  EXPECT_EQ(limiter.Peek("k").remaining, 2);
  ASSERT_TRUE(limiter.Acquire("k").allowed);
  EXPECT_EQ(limiter.Peek("k").remaining, 1);
  EXPECT_EQ(limiter.Peek("k").remaining, 1) << "Peek 不能改变状态";
  EXPECT_TRUE(limiter.Acquire("k").allowed);
}

TEST(RateLimiterTest, BucketCountIsBounded) {
  // 攻击者可以用海量随机 key 打内存，桶数量必须有硬上限，
  // 否则"限流器"本身就成了内存放大的突破口
  RateLimiter limiter(5, std::chrono::seconds(60), 32);
  for (int i = 0; i < 500; ++i) {
    (void)limiter.Acquire("key-" + std::to_string(i));
  }
  EXPECT_LE(limiter.Size(), 32U) << "桶数量必须被上限约束";
}

TEST(ClientIpTest, PrefersForwardedForThenRealIpThenPeer) {
  EXPECT_EQ(ClientIpFrom("203.0.113.7, 10.0.0.1", "10.0.0.2", "127.0.0.1"), "203.0.113.7")
      << "取最左边一段：那是最初的客户端";
  EXPECT_EQ(ClientIpFrom("", "10.0.0.2", "127.0.0.1"), "10.0.0.2");
  EXPECT_EQ(ClientIpFrom("", "", "127.0.0.1"), "127.0.0.1");
  EXPECT_EQ(ClientIpFrom("  ", "  10.0.0.9  ", "127.0.0.1"), "10.0.0.9") << "空白要被裁掉";
}

}  // namespace
}  // namespace lan_file_hub
