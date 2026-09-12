// 字节级限速器单元测试（阶段十二第 4 条）。纯内存实现，不依赖数据库与网络。
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "lan_file_hub/common/byte_rate_limiter.h"

namespace lan_file_hub {
namespace {

TEST(ByteRateLimiterTest, DisabledWhenRateIsZero) {
  ByteRateLimiter limiter(0);
  EXPECT_FALSE(limiter.enabled());
  const auto grant = limiter.Acquire("u", 10 * 1024 * 1024);
  EXPECT_EQ(grant.bytes, 10U * 1024 * 1024);
  EXPECT_EQ(grant.retry_after.count(), 0);
}

TEST(ByteRateLimiterTest, GrantsAtMostOneSecondOfBurst) {
  ByteRateLimiter limiter(1000);  // 1000 B/s → 突发额度 1000 字节
  const auto first = limiter.Acquire("u", 5000);
  EXPECT_EQ(first.bytes, 1000U) << "新桶最多先给 1 秒的额度，不能一次把大文件全放行";
  EXPECT_EQ(first.retry_after.count(), 0);
}

TEST(ByteRateLimiterTest, AsksToWaitWhenTokensAreGone) {
  ByteRateLimiter limiter(1);  // 1 B/s：1 字节要等 1 秒，回补速度可忽略，断言才稳定
  ASSERT_EQ(limiter.Acquire("u", 1).bytes, 1U);
  const auto drained = limiter.Acquire("u", 1);
  EXPECT_EQ(drained.bytes, 0U);
  EXPECT_GE(drained.retry_after.count(), 900);
  EXPECT_LE(drained.retry_after.count(), 1100);
}

TEST(ByteRateLimiterTest, RetryAfterIsProportionalToTheMissingByte) {
  ByteRateLimiter fast(1000);  // 1 字节 = 1ms
  ASSERT_EQ(fast.Acquire("u", 1000).bytes, 1000U);
  const auto grant = fast.Acquire("u", 1);
  ASSERT_EQ(grant.bytes, 0U);
  EXPECT_LE(grant.retry_after.count(), 5);
}

TEST(ByteRateLimiterTest, TokensRefillOverTime) {
  ByteRateLimiter limiter(100000);  // 100 KB/s
  ASSERT_EQ(limiter.Acquire("u", 100000).bytes, 100000U);
  const auto right_after = limiter.Acquire("u", 100000);
  EXPECT_LT(right_after.bytes, 1000U) << "刚取空，几乎不该再放行";

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const auto after_wait = limiter.Acquire("u", 100000);
  EXPECT_GE(after_wait.bytes, 5000U) << "等了 100ms，至少应该回补 10KB 的量级（留足抖动余量）";
}

TEST(ByteRateLimiterTest, KeysAreIndependent) {
  ByteRateLimiter limiter(1000);
  ASSERT_EQ(limiter.Acquire("alice", 1000).bytes, 1000U);
  // 取空后只可能回补极少量的字节（时间抖动），不能整份放行
  EXPECT_LT(limiter.Acquire("alice", 1000).bytes, 5U);
  EXPECT_EQ(limiter.Acquire("bob", 1000).bytes, 1000U) << "一个用户跑满不应影响另一个用户";
}

TEST(ByteRateLimiterTest, ResetReleasesTheBucket) {
  ByteRateLimiter limiter(1);  // 1 B/s：回补速度可忽略，断言才稳定
  ASSERT_EQ(limiter.Acquire("u", 1).bytes, 1U);
  ASSERT_EQ(limiter.Acquire("u", 1).bytes, 0U);
  limiter.Reset("u");
  EXPECT_EQ(limiter.Acquire("u", 1).bytes, 1U);
}

TEST(ByteRateLimiterTest, BucketCountIsBounded) {
  ByteRateLimiter limiter(1000, 16);
  for (int i = 0; i < 200; ++i) {
    (void)limiter.Acquire("key-" + std::to_string(i), 10);
  }
  EXPECT_LE(limiter.Size(), 16U) << "桶数量必须有上限，否则随机 key 就能打爆内存";
}

TEST(ByteRateLimiterTest, PartialGrantNeverExceedsTheRequest) {
  ByteRateLimiter limiter(1000);
  const auto grant = limiter.Acquire("u", 10);
  EXPECT_LE(grant.bytes, 10U);
  EXPECT_EQ(grant.bytes, 10U);
}

}  // namespace
}  // namespace lan_file_hub
