#include <cctype>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include "lan_file_hub/common/uuid.h"

namespace lan_file_hub {
namespace {

TEST(UuidTest, HasCanonicalShapeAndVersionBits) {
  const std::string id = GenerateUuidV4();
  ASSERT_EQ(id.size(), 36U);
  EXPECT_EQ(id[8], '-');
  EXPECT_EQ(id[13], '-');
  EXPECT_EQ(id[18], '-');
  EXPECT_EQ(id[23], '-');

  EXPECT_EQ(id[14], '4') << "第 7 字节的高 4 位必须是版本号 4";

  const char variant = id[19];
  EXPECT_TRUE(variant == '8' || variant == '9' || variant == 'a' || variant == 'b')
      << "变体位必须落在 RFC 4122 区间，实际: " << variant;

  for (const char c : id) {
    if (c == '-') {
      continue;
    }
    EXPECT_NE(std::isxdigit(static_cast<unsigned char>(c)), 0);
    EXPECT_EQ(std::isupper(static_cast<unsigned char>(c)), 0) << "统一使用小写";
  }
}

TEST(UuidTest, IsUniqueAcrossManyCalls) {
  std::set<std::string> seen;
  for (int i = 0; i < 1000; ++i) {
    seen.insert(GenerateUuidV4());
  }
  EXPECT_EQ(seen.size(), 1000U);
}

}  // namespace
}  // namespace lan_file_hub
