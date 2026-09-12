#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "lan_file_hub/domain/folder.h"

namespace lan_file_hub {
namespace {

TEST(FolderNameTest, AcceptsOrdinaryNames) {
  const std::vector<std::string> names = {"documents", "我的文档",
                                          "a.b",       "2026-09-12",
                                          "a b c",     std::string(kMaxFolderNameLength, 'x')};
  for (const auto& name : names) {
    const auto result = ValidateFolderName(name);
    EXPECT_TRUE(result.Ok()) << "应接受: " << name;
    EXPECT_EQ(result.Value(), name);
  }
}

TEST(FolderNameTest, TrimsSurroundingWhitespace) {
  const auto result = ValidateFolderName("  documents  ");
  ASSERT_TRUE(result.Ok());
  EXPECT_EQ(result.Value(), "documents");
}

TEST(FolderNameTest, RejectsEmptyAndWhitespaceOnly) {
  for (const char* name : {"", "   ", "\t", "\n"}) {
    const auto result = ValidateFolderName(name);
    EXPECT_FALSE(result.Ok()) << "应拒绝: [" << name << "]";
    EXPECT_EQ(result.GetError().code, "INVALID_ARGUMENT");
  }
}

TEST(FolderNameTest, RejectsDotAndDotDot) {
  for (const char* name : {".", "..", " . ", " .. "}) {
    const auto result = ValidateFolderName(name);
    EXPECT_FALSE(result.Ok()) << "应拒绝: " << name;
    EXPECT_NE(result.GetError().message.find("不能是"), std::string::npos);
  }
}

TEST(FolderNameTest, RejectsPathSeparators) {
  for (const char* name : {"a/b", "a\\b", "/", "\\", "a/../b"}) {
    const auto result = ValidateFolderName(name);
    EXPECT_FALSE(result.Ok()) << "应拒绝: " << name;
    EXPECT_NE(result.GetError().message.find("路径分隔符"), std::string::npos);
  }
}

TEST(FolderNameTest, RejectsControlCharacters) {
  for (const std::string name :
       {std::string("a") + '\0' + "b", std::string("a\tb"), std::string("a\rb"),
        std::string("a\x7F"
                    "b")}) {
    const auto result = ValidateFolderName(name);
    EXPECT_FALSE(result.Ok()) << "应拒绝含控制字符的名称";
  }
}

TEST(FolderNameTest, RejectsOverlongName) {
  const auto result = ValidateFolderName(std::string(kMaxFolderNameLength + 1, 'x'));
  ASSERT_FALSE(result.Ok());
  EXPECT_NE(result.GetError().message.find("过长"), std::string::npos);
}

TEST(FolderStatusTest, RoundTripsThroughDbStrings) {
  for (const FolderStatus status :
       {FolderStatus::kActive, FolderStatus::kTrashed, FolderStatus::kPurged}) {
    EXPECT_EQ(ParseFolderStatus(ToDbString(status)), status);
  }
  EXPECT_EQ(ToDbString(FolderStatus::kActive), "ACTIVE");
  EXPECT_EQ(ToDbString(FolderStatus::kTrashed), "TRASHED");
  EXPECT_EQ(ToDbString(FolderStatus::kPurged), "PURGED");
}

TEST(FolderStatusTest, ThrowsOnUnknownValue) {
  EXPECT_THROW({ (void)ParseFolderStatus("DELETED"); }, std::invalid_argument);
}

}  // namespace
}  // namespace lan_file_hub
