#include <string>

#include <gtest/gtest.h>

#include "lan_file_hub/common/request_id.h"

namespace lan_file_hub {
namespace {

TEST(RequestIdTest, AcceptsWellFormedIds) {
  EXPECT_TRUE(IsValidRequestId("abc-123_XYZ.9"));
  EXPECT_TRUE(IsValidRequestId("a"));
  EXPECT_TRUE(IsValidRequestId(std::string(64, 'a')));
}

TEST(RequestIdTest, RejectsEmptyOverlongAndIllegalChars) {
  EXPECT_FALSE(IsValidRequestId(""));
  EXPECT_FALSE(IsValidRequestId(std::string(65, 'a')));
  EXPECT_FALSE(IsValidRequestId("has space"));
  EXPECT_FALSE(IsValidRequestId("has/slash"));
  EXPECT_FALSE(IsValidRequestId("换行\n注入"));
  EXPECT_FALSE(IsValidRequestId("中文标识"));
  EXPECT_FALSE(IsValidRequestId("semi;colon"));
}

TEST(RequestIdTest, KeepsClientIdWhenValid) {
  EXPECT_EQ(ResolveRequestId("client-trace-001"), "client-trace-001");
}

TEST(RequestIdTest, GeneratesUuidWhenInvalidOrMissing) {
  const std::string generated = ResolveRequestId("");
  EXPECT_EQ(generated.size(), 36U);
  EXPECT_NE(generated.find('-'), std::string::npos);

  const std::string from_bad = ResolveRequestId("非法 id");
  EXPECT_EQ(from_bad.size(), 36U);
  EXPECT_NE(from_bad, generated);
}

TEST(RequestIdTest, ThreadLocalSetGetClear) {
  EXPECT_TRUE(CurrentRequestId().empty());
  SetCurrentRequestId("req-1");
  EXPECT_EQ(CurrentRequestId(), "req-1");
  ClearCurrentRequestId();
  EXPECT_TRUE(CurrentRequestId().empty());
}

}  // namespace
}  // namespace lan_file_hub
