// 阶段五领域规则单元测试：分块数量、分块大小、会话状态机、分块路径安全。
// 不依赖数据库与磁盘。
#include <cstdint>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "lan_file_hub/domain/upload_session.h"
#include "lan_file_hub/storage/object_storage.h"

namespace lan_file_hub {
namespace {

constexpr std::uint32_t kMiB = 1024U * 1024U;

// ---- 分块数量 ---------------------------------------------------------------

TEST(ComputeTotalChunksTest, ZeroByteFileStillHasOneChunk) {
  // 0 字节文件也必须有一个（空）分块，否则 complete 时无从合并
  EXPECT_EQ(ComputeTotalChunks(0, 8 * kMiB), 1U);
}

TEST(ComputeTotalChunksTest, ExactMultipleDoesNotAddExtraChunk) {
  EXPECT_EQ(ComputeTotalChunks(8 * kMiB, 8 * kMiB), 1U);
  EXPECT_EQ(ComputeTotalChunks(16 * kMiB, 8 * kMiB), 2U);
  EXPECT_EQ(ComputeTotalChunks(24 * kMiB, 8 * kMiB), 3U);
}

TEST(ComputeTotalChunksTest, RoundsUpPartialChunk) {
  EXPECT_EQ(ComputeTotalChunks(8 * kMiB + 1, 8 * kMiB), 2U);
  EXPECT_EQ(ComputeTotalChunks(1, 8 * kMiB), 1U);
  // 20 MiB / 8 MiB → 3 块（8 + 8 + 4）
  EXPECT_EQ(ComputeTotalChunks(20 * kMiB, 8 * kMiB), 3U);
}

TEST(ComputeTotalChunksTest, UsesSmallestAllowedChunkSize) {
  EXPECT_EQ(ComputeTotalChunks(64 * kMiB, kMiB), 64U);
  EXPECT_EQ(ComputeTotalChunks(64 * kMiB + 1, kMiB), 65U);
}

// ---- 分块大小校验 -----------------------------------------------------------

TEST(ValidateChunkSizeTest, AcceptsBoundariesAndDefault) {
  EXPECT_TRUE(ValidateChunkSize(kMiB).Ok()) << "1 MiB 是下界（含）";
  EXPECT_TRUE(ValidateChunkSize(64 * kMiB).Ok()) << "64 MiB 是上界（含）";
  EXPECT_TRUE(ValidateChunkSize(8 * kMiB).Ok()) << "默认值";
}

TEST(ValidateChunkSizeTest, RejectsOutOfRange) {
  for (const std::uint32_t size : {0U, 1U, kMiB - 1, 64 * kMiB + 1, 128 * kMiB}) {
    const auto status = ValidateChunkSize(size);
    ASSERT_FALSE(status.Ok()) << "应拒绝分块大小: " << size;
    EXPECT_EQ(status.GetError().code, "INVALID_ARGUMENT");
  }
}

// ---- 状态机 -----------------------------------------------------------------

TEST(UploadStatusTest, DbStringRoundTrip) {
  const UploadStatus all[] = {
      UploadStatus::kInitiated, UploadStatus::kUploading, UploadStatus::kMerging,
      UploadStatus::kVerifying, UploadStatus::kCompleted, UploadStatus::kFailed,
      UploadStatus::kAborted,   UploadStatus::kExpired,
  };
  for (const auto status : all) {
    const std::string text = ToDbString(status);
    EXPECT_FALSE(text.empty());
    EXPECT_EQ(ParseUploadStatus(text), status) << "状态字符串必须能往返: " << text;
  }
  // 与迁移里的 CHECK 约束保持一致：数据库只接受这 8 个大写值
  EXPECT_EQ(ToDbString(UploadStatus::kInitiated), "INITIATED");
  EXPECT_EQ(ToDbString(UploadStatus::kCompleted), "COMPLETED");
  EXPECT_EQ(ToDbString(UploadStatus::kExpired), "EXPIRED");
}

TEST(UploadStatusTest, UnknownStringIsRejectedLoudly) {
  // 迁移里的 CHECK 约束保证不会出现未知值；真出现了要立刻暴露，
  // 不能悄悄当成 INITIATED 继续往一个状态坏掉的会话里写分块。
  // 仓储层会把这个异常转成错误返回给调用方。
  EXPECT_THROW((void)ParseUploadStatus("NOT_A_STATUS"), std::invalid_argument);
  EXPECT_THROW((void)ParseUploadStatus("initiated"), std::invalid_argument) << "必须区分大小写";
  EXPECT_THROW((void)ParseUploadStatus(""), std::invalid_argument);
}

TEST(UploadStatusTest, InProgressAndTerminalAreDisjoint) {
  const UploadStatus all[] = {
      UploadStatus::kInitiated, UploadStatus::kUploading, UploadStatus::kMerging,
      UploadStatus::kVerifying, UploadStatus::kCompleted, UploadStatus::kFailed,
      UploadStatus::kAborted,   UploadStatus::kExpired,
  };
  for (const auto status : all) {
    EXPECT_FALSE(IsInProgress(status) && IsTerminal(status))
        << "状态不能既在进行中又是终态: " << ToDbString(status);
  }
  // 还能继续收分块的只有这两个
  EXPECT_TRUE(IsInProgress(UploadStatus::kInitiated));
  EXPECT_TRUE(IsInProgress(UploadStatus::kUploading));
  // 合并与校验阶段既不收分块、也还没结束：两个判定都必须为假，
  // 否则要么会接受新分块，要么会被当成终态而拒绝幂等的 complete
  EXPECT_FALSE(IsInProgress(UploadStatus::kMerging));
  EXPECT_FALSE(IsInProgress(UploadStatus::kVerifying));
  EXPECT_FALSE(IsTerminal(UploadStatus::kMerging));
  EXPECT_FALSE(IsTerminal(UploadStatus::kVerifying));
  for (const auto status : {UploadStatus::kCompleted, UploadStatus::kFailed, UploadStatus::kAborted,
                            UploadStatus::kExpired}) {
    EXPECT_TRUE(IsTerminal(status)) << ToDbString(status);
    EXPECT_FALSE(IsInProgress(status)) << ToDbString(status);
  }
}

// ---- 分块路径安全 -----------------------------------------------------------

TEST(MakeChunkKeyTest, BuildsKeyUnderTmpUploads) {
  const auto key = MakeChunkKey("3f2504e0-4f89-11d3-9a0c-0305e82c3301", 7);
  ASSERT_TRUE(key.Ok()) << key.GetError().message;
  EXPECT_EQ(key.Value(), "tmp/uploads/3f2504e0-4f89-11d3-9a0c-0305e82c3301/7");
  EXPECT_EQ(key.Value().rfind("tmp/", 0), 0U) << "分块只能落在临时目录";
}

TEST(MakeChunkKeyTest, RejectsUnsafeUploadId) {
  const char* bad[] = {
      "",                  // 空
      "../../etc/passwd",  // 目录穿越
      "abc/def",           // 路径分隔符
      "ABC",               // 大写（uuid 一律小写）
      "abc def",           // 空格
      "abc\n",             // 控制字符
      "上传会话",          // 非 ASCII
      "abc\\def",          // 反斜杠
  };
  for (const char* id : bad) {
    EXPECT_FALSE(MakeChunkKey(id, 0).Ok()) << "应拒绝 upload_id: " << id;
  }
  // 超长
  EXPECT_FALSE(MakeChunkKey(std::string(65, 'a'), 0).Ok());
  EXPECT_TRUE(MakeChunkKey(std::string(64, 'a'), 0).Ok()) << "64 字符是允许的上界";
}

TEST(MakeChunkKeyTest, IndexIsPlainDecimal) {
  EXPECT_EQ(MakeChunkKey("abcdef", 0).Value(), "tmp/uploads/abcdef/0");
  EXPECT_EQ(MakeChunkKey("abcdef", 4294967295U).Value(), "tmp/uploads/abcdef/4294967295")
      << "索引由服务端校验，不进用户输入";
}

}  // namespace
}  // namespace lan_file_hub
