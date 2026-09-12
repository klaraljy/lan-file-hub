#include <string>

#include <gtest/gtest.h>

#include "lan_file_hub/infrastructure/password_hasher.h"

namespace lan_file_hub {
namespace {

// 测试里把参数调小，否则每个用例要跑 64 MiB / 3 轮，测试会非常慢
Argon2Options FastOptions() {
  Argon2Options options;
  options.memory_cost_kib = 1024;  // 1 MiB
  options.time_cost = 1;
  return options;
}

TEST(PasswordHasherTest, ProducesArgon2idPhcString) {
  PasswordHasher hasher(FastOptions());
  const auto encoded = hasher.Hash("correct-horse-battery");
  ASSERT_TRUE(encoded.Ok()) << encoded.GetError().message;
  // PHC 格式：$argon2id$v=19$m=...,t=...,p=...$<salt>$<hash>
  EXPECT_EQ(encoded.Value().rfind("$argon2id$", 0), 0U);
  EXPECT_NE(encoded.Value().find("$v=19$"), std::string::npos);
}

TEST(PasswordHasherTest, NeverStoresPlaintext) {
  PasswordHasher hasher(FastOptions());
  const std::string password = "correct-horse-battery";
  const auto encoded = hasher.Hash(password);
  ASSERT_TRUE(encoded.Ok());
  EXPECT_EQ(encoded.Value().find(password), std::string::npos) << "编码串里绝不能出现明文口令";
}

TEST(PasswordHasherTest, SamePasswordProducesDifferentHashes) {
  PasswordHasher hasher(FastOptions());
  const auto first = hasher.Hash("correct-horse-battery");
  const auto second = hasher.Hash("correct-horse-battery");
  ASSERT_TRUE(first.Ok());
  ASSERT_TRUE(second.Ok());
  EXPECT_NE(first.Value(), second.Value()) << "每次都必须使用新的随机盐";
}

TEST(PasswordHasherTest, VerifiesCorrectPasswordAndRejectsWrongOne) {
  PasswordHasher hasher(FastOptions());
  const auto encoded = hasher.Hash("correct-horse-battery");
  ASSERT_TRUE(encoded.Ok());

  EXPECT_TRUE(hasher.Verify("correct-horse-battery", encoded.Value()));
  EXPECT_FALSE(hasher.Verify("correct-horse-battery ", encoded.Value()));
  EXPECT_FALSE(hasher.Verify("Correct-Horse-Battery", encoded.Value()));
  EXPECT_FALSE(hasher.Verify("", encoded.Value()));
}

TEST(PasswordHasherTest, RejectsMalformedOrEmptyHashWithoutThrowing) {
  PasswordHasher hasher(FastOptions());
  EXPECT_FALSE(hasher.Verify("whatever", ""));
  EXPECT_FALSE(hasher.Verify("whatever", "not-a-phc-string"));
  EXPECT_FALSE(hasher.Verify("whatever", "$argon2id$v=19$broken"));
}

TEST(PasswordHasherTest, EnforcesPasswordLengthBounds) {
  PasswordHasher hasher(FastOptions());

  const auto too_short = hasher.Hash("short");
  ASSERT_FALSE(too_short.Ok());
  EXPECT_EQ(too_short.GetError().code, "INVALID_ARGUMENT");

  const auto too_long = hasher.Hash(std::string(kMaxPasswordBytes + 1, 'x'));
  ASSERT_FALSE(too_long.Ok());
  EXPECT_EQ(too_long.GetError().code, "INVALID_ARGUMENT");

  const auto at_limit = hasher.Hash(std::string(kMaxPasswordBytes, 'x'));
  EXPECT_TRUE(at_limit.Ok()) << "恰好到上限应当允许";
}

TEST(PasswordHasherTest, VerifiesAgainstDefaultCostHash) {
  // 默认参数（生产参数）也要能自洽
  PasswordHasher hasher;
  const auto encoded = hasher.Hash("correct-horse-battery");
  ASSERT_TRUE(encoded.Ok());
  EXPECT_TRUE(hasher.Verify("correct-horse-battery", encoded.Value()));
}

}  // namespace
}  // namespace lan_file_hub
