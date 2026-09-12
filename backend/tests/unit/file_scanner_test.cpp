// FileScanner 抽象单元测试（阶段十二第 17 条）。纯内存实现。
#include <string>

#include <gtest/gtest.h>

#include "lan_file_hub/application/file_scanner.h"

namespace lan_file_hub {
namespace {

TEST(FileScannerTest, NoopScannerReportsSkippedNotClean) {
  NoopFileScanner scanner;
  auto result = scanner.Scan("objects/ab/abcdef", "abcdef");
  ASSERT_TRUE(result.Ok());
  // 关键语义：没有扫描能力时必须报 skipped，不能报 clean —— 否则就是假装做过安全检测
  EXPECT_EQ(result.Value().verdict, ScanVerdict::kSkipped);
  EXPECT_NE(result.Value().verdict, ScanVerdict::kClean);
  EXPECT_FALSE(result.Value().detail.empty()) << "要说明为什么没扫";
}

TEST(FileScannerTest, NoopScannerHasStableName) {
  NoopFileScanner scanner;
  EXPECT_EQ(scanner.name(), "noop");
}

TEST(FileScannerTest, InterfaceIsPolymorphic) {
  // 业务层只依赖抽象：换成任何实现的写法必须成立
  std::shared_ptr<FileScanner> scanner = std::make_shared<NoopFileScanner>();
  auto result = scanner->Scan("objects/00/00", "00");
  ASSERT_TRUE(result.Ok());
  EXPECT_EQ(scanner->name(), "noop");
}

}  // namespace
}  // namespace lan_file_hub
