#include <string>

#include <gtest/gtest.h>

#include "lan_file_hub/common/http_util.h"

namespace lan_file_hub {
namespace {

constexpr std::uint64_t kSize = 1000;

TEST(ByteRangeTest, EmptyHeaderMeansWholeFile) {
  const auto range = ParseByteRange("", kSize);
  ASSERT_TRUE(range.Ok());
  EXPECT_EQ(range.Value().start, 0U);
  EXPECT_EQ(range.Value().length, kSize);
}

TEST(ByteRangeTest, ParsesClosedRange) {
  const auto range = ParseByteRange("bytes=0-499", kSize);
  ASSERT_TRUE(range.Ok());
  EXPECT_EQ(range.Value().start, 0U);
  EXPECT_EQ(range.Value().length, 500U);
}

TEST(ByteRangeTest, ParsesOpenEndedRange) {
  const auto range = ParseByteRange("bytes=500-", kSize);
  ASSERT_TRUE(range.Ok());
  EXPECT_EQ(range.Value().start, 500U);
  EXPECT_EQ(range.Value().length, 500U);
}

TEST(ByteRangeTest, ParsesSuffixRange) {
  const auto range = ParseByteRange("bytes=-200", kSize);
  ASSERT_TRUE(range.Ok());
  EXPECT_EQ(range.Value().start, 800U);
  EXPECT_EQ(range.Value().length, 200U);
}

TEST(ByteRangeTest, SuffixLargerThanFileClampsToWholeFile) {
  const auto range = ParseByteRange("bytes=-5000", kSize);
  ASSERT_TRUE(range.Ok());
  EXPECT_EQ(range.Value().start, 0U);
  EXPECT_EQ(range.Value().length, kSize);
}

TEST(ByteRangeTest, ClampsEndBeyondFileSize) {
  const auto range = ParseByteRange("bytes=900-5000", kSize);
  ASSERT_TRUE(range.Ok());
  EXPECT_EQ(range.Value().start, 900U);
  EXPECT_EQ(range.Value().length, 100U);
}

TEST(ByteRangeTest, StartBeyondFileSizeIsRejected) {
  const auto range = ParseByteRange("bytes=2000-3000", kSize);
  ASSERT_FALSE(range.Ok()) << "起点越界必须让调用方回 416";
}

TEST(ByteRangeTest, RejectsMalformedRanges) {
  for (const char* header : {"bytes=", "bytes=-", "bytes=abc-def", "bytes=500-100", "bytes=abc-",
                             "chars=0-10", "bytes=1-2-3"}) {
    const auto range = ParseByteRange(header, kSize);
    // 非法写法要么被拒绝（416），要么被安全地忽略为整文件
    if (range.Ok()) {
      EXPECT_EQ(range.Value().start, 0U) << "忽略 Range 时必须返回整个文件: " << header;
      EXPECT_EQ(range.Value().length, kSize) << header;
    }
  }
}

TEST(ByteRangeTest, MultiRangeFallsBackToWholeFile) {
  // 多段 range 不支持：按规范可以忽略，这里选择返回整个文件而不是报错
  const auto range = ParseByteRange("bytes=0-10,20-30", kSize);
  ASSERT_TRUE(range.Ok());
  EXPECT_EQ(range.Value().start, 0U);
  EXPECT_EQ(range.Value().length, kSize);
}

TEST(ByteRangeTest, ZeroSizeFileYieldsEmptyRange) {
  const auto range = ParseByteRange("bytes=0-10", 0);
  ASSERT_TRUE(range.Ok());
  EXPECT_EQ(range.Value().length, 0U);
}

TEST(ContentDispositionTest, AsciiNameUsesSimpleForm) {
  const std::string header = BuildContentDisposition("report.pdf");
  EXPECT_EQ(header, "attachment; filename=\"report.pdf\"");
}

TEST(ContentDispositionTest, ChineseNameAddsEncodedForm) {
  const std::string header = BuildContentDisposition("中文.txt");
  EXPECT_NE(header.find("filename*=UTF-8''"), std::string::npos);
  EXPECT_NE(header.find("%E4%B8%AD%E6%96%87.txt"), std::string::npos);
  // 回退名必须是纯 ASCII，否则响应头非法
  for (const char c : header) {
    EXPECT_TRUE(static_cast<unsigned char>(c) < 0x80) << "响应头里不能出现非 ASCII 字节";
  }
}

TEST(ContentDispositionTest, StripsQuotesAndBackslashesFromFallback) {
  const std::string header = BuildContentDisposition("evil\"; drop\\table.txt");
  // 回退名里的引号与反斜杠都被替换掉，不会截断响应头
  EXPECT_EQ(header.find("evil\""), std::string::npos);
  EXPECT_NE(header.find("filename="), std::string::npos);
}

TEST(ContentDispositionTest, EmptyNameGetsDefault) {
  const std::string header = BuildContentDisposition("");
  EXPECT_NE(header.find("download.bin"), std::string::npos);
}

}  // namespace
}  // namespace lan_file_hub
