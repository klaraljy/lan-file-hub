#include <string>

#include <gtest/gtest.h>

#include "lan_file_hub/common/pagination.h"

namespace lan_file_hub {
namespace {

TEST(PaginationTest, UsesDefaultsWhenNothingProvided) {
  const auto result = ParsePagination("", "", "", "");
  ASSERT_TRUE(result.Ok());
  EXPECT_EQ(result.Value().page, 1);
  EXPECT_EQ(result.Value().page_size, kDefaultPageSize);
  EXPECT_EQ(result.Value().sort_by, "name");
  EXPECT_EQ(result.Value().sort_order, "asc");
}

TEST(PaginationTest, ParsesProvidedValues) {
  const auto result = ParsePagination("3", "25", "Created_At", "DESC");
  ASSERT_TRUE(result.Ok());
  EXPECT_EQ(result.Value().page, 3);
  EXPECT_EQ(result.Value().page_size, 25);
  EXPECT_EQ(result.Value().sort_by, "created_at");
  EXPECT_EQ(result.Value().sort_order, "desc");
  EXPECT_EQ(result.Value().Offset(), 50);
}

TEST(PaginationTest, EnforcesPageSizeBounds) {
  EXPECT_FALSE(ParsePagination("1", "0", "", "").Ok());
  EXPECT_FALSE(ParsePagination("1", "101", "", "").Ok());
  EXPECT_FALSE(ParsePagination("1", "-5", "", "").Ok());
  EXPECT_TRUE(ParsePagination("1", "100", "", "").Ok()) << "上限本身应当允许";
}

TEST(PaginationTest, RejectsNonNumericAndZeroPage) {
  EXPECT_FALSE(ParsePagination("abc", "", "", "").Ok());
  EXPECT_FALSE(ParsePagination("0", "", "", "").Ok());
}

TEST(PaginationTest, RejectsUnknownSortOrder) {
  const auto result = ParsePagination("1", "10", "name", "sideways");
  ASSERT_FALSE(result.Ok());
  EXPECT_NE(result.GetError().message.find("sort_order"), std::string::npos);
}

TEST(PaginationTest, BuildOrderByMapsWhitelistedFieldToFixedSql) {
  Pagination pagination;
  pagination.sort_by = "name";
  pagination.sort_order = "asc";

  const auto sql =
      BuildOrderBy(pagination, {{"name", "lower(f.name)"}, {"created_at", "f.created_at"}});
  ASSERT_TRUE(sql.Ok());
  EXPECT_EQ(sql.Value(), "lower(f.name) ASC");
}

TEST(PaginationTest, BuildOrderByRejectsFieldOutsideWhitelist) {
  Pagination pagination;
  // 典型注入尝试：字段名不在白名单
  pagination.sort_by = "name; drop table users";

  const auto sql = BuildOrderBy(pagination, {{"name", "lower(f.name)"}});
  ASSERT_FALSE(sql.Ok());
  EXPECT_EQ(sql.GetError().code, "INVALID_ARGUMENT");
}

TEST(PaginationTest, BuildOrderByAppendsDesc) {
  Pagination pagination;
  pagination.sort_by = "updated_at";
  pagination.sort_order = "desc";

  const auto sql = BuildOrderBy(pagination, {{"updated_at", "f.updated_at"}});
  ASSERT_TRUE(sql.Ok());
  EXPECT_EQ(sql.Value(), "f.updated_at DESC");
}

}  // namespace
}  // namespace lan_file_hub
