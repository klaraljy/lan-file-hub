#include <string>

#include <gtest/gtest.h>

#include "lan_file_hub/common/sha256.h"

namespace lan_file_hub {
namespace {

TEST(Sha256Test, MatchesKnownVectors) {
  // 标准测试向量
  EXPECT_EQ(Sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(Sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(Sha256Hex("hello lan-file-hub"),
            Sha256Hex(std::string("hello").append(" lan-file-hub")));
}

TEST(Sha256Test, StreamingInChunksEqualsOneShot) {
  std::string payload;
  for (int i = 0; i < 100000; ++i) {
    payload.push_back(static_cast<char>('a' + (i % 26)));
  }
  const std::string expected = Sha256Hex(payload);

  Sha256Hasher hasher;
  // 故意用不均匀的分块，验证流式与一次性结果一致
  std::size_t offset = 0;
  const std::size_t steps[] = {1, 7, 4096, 65536, 3};
  std::size_t step = 0;
  while (offset < payload.size()) {
    const std::size_t length = std::min(steps[step % 5], payload.size() - offset);
    ASSERT_TRUE(hasher.Update(payload.data() + offset, length).Ok());
    offset += length;
    ++step;
  }
  const auto digest = hasher.HexDigest();
  ASSERT_TRUE(digest.Ok());
  EXPECT_EQ(digest.Value(), expected);
}

TEST(Sha256Test, ProducesLowercaseHexOfCorrectLength) {
  const std::string hex = Sha256Hex("anything");
  ASSERT_EQ(hex.size(), 64U);
  for (const char c : hex) {
    EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) << "非小写十六进制: " << c;
  }
}

TEST(Sha256Test, RefusesDoubleDigest) {
  Sha256Hasher hasher;
  ASSERT_TRUE(hasher.Update("abc", 3).Ok());
  ASSERT_TRUE(hasher.HexDigest().Ok());
  EXPECT_FALSE(hasher.HexDigest().Ok()) << "同一实例不应能取两次摘要";
  EXPECT_FALSE(hasher.Update("more", 4).Ok()) << "取完摘要后不应还能追加";
}

TEST(Sha256Test, EmptyUpdateDoesNotChangeResult) {
  Sha256Hasher with_empty;
  ASSERT_TRUE(with_empty.Update("", 0).Ok());
  ASSERT_TRUE(with_empty.Update("abc", 3).Ok());
  ASSERT_TRUE(with_empty.Update("", 0).Ok());

  const auto digest = with_empty.HexDigest();
  ASSERT_TRUE(digest.Ok());
  EXPECT_EQ(digest.Value(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

}  // namespace
}  // namespace lan_file_hub
