// 搜索关键字的 LIKE 模式构造（2026-10-03）：用户输入里的 `%` 与 `_` 必须当成普通字符。
//
// 为什么值得单独测：不转义的话，用户在搜索框里敲一个 `%` 就会命中**所有**文件
// （看起来像"搜索坏了"），敲 `_` 会匹配任意单字符 —— 既是功能缺陷也是注入面。
#include <string>

#include <gtest/gtest.h>

#include "lan_file_hub/common/sql_like.h"

namespace lan_file_hub {
namespace {

TEST(SqlLikeTest, EscapesWildcardsAndBackslash) {
  EXPECT_EQ(EscapeLikePattern("report"), "report");
  EXPECT_EQ(EscapeLikePattern("100%"), "100\\%");
  EXPECT_EQ(EscapeLikePattern("a_b"), "a\\_b");
  EXPECT_EQ(EscapeLikePattern("c:\\temp"), "c:\\\\temp");
  // 三个一起出现时每一个都要转义
  EXPECT_EQ(EscapeLikePattern("50%_\\"), "50\\%\\_\\\\");
}

TEST(SqlLikeTest, ContainsWrapsTheEscapedSubstring) {
  EXPECT_EQ(LikeContains("报表"), "%报表%");
  EXPECT_EQ(LikeContains("50%"), "%50\\%%");
  EXPECT_EQ(LikeContains(""), "%%");
}

TEST(SqlLikeTest, TrimsSurroundingWhitespaceOnly) {
  EXPECT_EQ(TrimForSearch("  report  "), "report");
  EXPECT_EQ(TrimForSearch("\t\n 报表 \r\n"), "报表");
  EXPECT_EQ(TrimForSearch("报表 2026"), "报表 2026") << "中间的空格要保留";
  EXPECT_EQ(TrimForSearch("   "), "");
  EXPECT_EQ(TrimForSearch(""), "");
}

}  // namespace
}  // namespace lan_file_hub
