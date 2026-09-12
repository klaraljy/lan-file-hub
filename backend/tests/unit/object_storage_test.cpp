#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "fixtures/temp_dir.h"
#include "lan_file_hub/common/sha256.h"
#include "lan_file_hub/storage/local_object_storage.h"
#include "lan_file_hub/storage/object_storage.h"

namespace lan_file_hub {
namespace {

TEST(ObjectKeyTest, AcceptsWellFormedKeys) {
  for (const char* key : {"objects/ab/abcd", "tmp/1234-5678", "a/b/c.txt",
                          "objects/2d/"
                          "2df786"}) {
    EXPECT_TRUE(ValidateObjectKey(key).Ok()) << "应接受: " << key;
  }
}

TEST(ObjectKeyTest, RejectsTraversalAndAbsolutePaths) {
  const char* bad_keys[] = {
      "",                     // 空
      "/etc/passwd",          // 绝对路径
      "C:/windows/system32",  // 盘符
      "objects/../../etc/x",  // 目录穿越
      "..",                   // 单段 ..
      "objects/./x",          // 单段 .
      "objects//double",      // 空段
      "objects\\windows",     // 反斜杠
      "objects/中文",         // 非 ASCII
      "objects/na me",        // 空格
      "objects/a\nb",         // 控制字符
  };
  for (const char* key : bad_keys) {
    EXPECT_FALSE(ValidateObjectKey(key).Ok()) << "应拒绝: " << key;
  }
}

TEST(ObjectKeyTest, RejectsOverlongKey) {
  const std::string long_key(kMaxObjectKeyLength + 1, 'a');
  EXPECT_FALSE(ValidateObjectKey(long_key).Ok());
}

TEST(ObjectKeyTest, BuildsContentAddressedKey) {
  const std::string hash(64, 'a');
  const auto key = MakeObjectKey(hash);
  ASSERT_TRUE(key.Ok());
  EXPECT_EQ(key.Value(), "objects/aa/" + hash);
}

TEST(ObjectKeyTest, RefusesToBuildKeyFromBadHash) {
  EXPECT_FALSE(MakeObjectKey("short").Ok());
  EXPECT_FALSE(MakeObjectKey(std::string(64, 'Z')).Ok()) << "必须是小写十六进制";
  EXPECT_FALSE(MakeObjectKey(std::string(64, ' ')).Ok());
}

TEST(LocalObjectStorageTest, WritesCommitsAndReadsBack) {
  testing::TempDir dir("storage");
  auto storage = LocalObjectStorage::Create(dir.Path());
  ASSERT_TRUE(storage.Ok()) << storage.GetError().message;

  auto writer = storage.Value()->CreateWriter();
  ASSERT_TRUE(writer.Ok());
  const std::string payload = "hello object storage";
  ASSERT_TRUE(writer.Value()->Write(payload.data(), payload.size()).Ok());
  ASSERT_TRUE(writer.Value()->Finish().Ok());
  EXPECT_EQ(writer.Value()->bytes_written(), payload.size());

  const std::string temp_key(writer.Value()->temp_key());
  EXPECT_EQ(temp_key.rfind("tmp/", 0), 0U);

  const auto final_key = MakeObjectKey(Sha256Hex(payload));
  ASSERT_TRUE(final_key.Ok());
  ASSERT_TRUE(storage.Value()->Commit(temp_key, final_key.Value()).Ok());

  EXPECT_TRUE(storage.Value()->Exists(final_key.Value()));
  auto size = storage.Value()->Size(final_key.Value());
  ASSERT_TRUE(size.Ok());
  EXPECT_EQ(size.Value(), payload.size());

  auto path = storage.Value()->ResolveDownloadPath(final_key.Value());
  ASSERT_TRUE(path.Ok());
  std::ifstream input(path.Value(), std::ios::binary);
  std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  EXPECT_EQ(content, payload);

  // 提交后临时对象必须消失
  EXPECT_FALSE(storage.Value()->Exists(temp_key));
}

TEST(LocalObjectStorageTest, CommittingSameContentTwiceReusesTheObject) {
  testing::TempDir dir("storage_dedup");
  auto storage = LocalObjectStorage::Create(dir.Path());
  ASSERT_TRUE(storage.Ok());

  const std::string payload = "duplicate content";
  const auto final_key = MakeObjectKey(Sha256Hex(payload));
  ASSERT_TRUE(final_key.Ok());

  for (int i = 0; i < 2; ++i) {
    auto writer = storage.Value()->CreateWriter();
    ASSERT_TRUE(writer.Ok());
    ASSERT_TRUE(writer.Value()->Write(payload.data(), payload.size()).Ok());
    ASSERT_TRUE(writer.Value()->Finish().Ok());
    ASSERT_TRUE(storage.Value()->Commit(writer.Value()->temp_key(), final_key.Value()).Ok());
  }

  auto size = storage.Value()->Size(final_key.Value());
  ASSERT_TRUE(size.Ok());
  EXPECT_EQ(size.Value(), payload.size());
}

TEST(LocalObjectStorageTest, DeleteIsIdempotentAndReportsMissingObject) {
  testing::TempDir dir("storage_delete");
  auto storage = LocalObjectStorage::Create(dir.Path());
  ASSERT_TRUE(storage.Ok());

  const auto key = MakeObjectKey(std::string(64, 'b'));
  ASSERT_TRUE(key.Ok());
  // 删除不存在的对象不算错误（幂等）
  EXPECT_TRUE(storage.Value()->Delete(key.Value()).Ok());
  EXPECT_FALSE(storage.Value()->Exists(key.Value()));

  auto size = storage.Value()->Size(key.Value());
  EXPECT_FALSE(size.Ok()) << "读不存在的对象必须报错，不能返回 0";
}

TEST(LocalObjectStorageTest, ResolveRejectsTraversalOutsideRoot) {
  testing::TempDir dir("storage_traversal");
  auto storage = LocalObjectStorage::Create(dir.Path());
  ASSERT_TRUE(storage.Ok());

  for (const char* key : {"../outside.txt", "objects/../../outside.txt", "/abs/path"}) {
    EXPECT_FALSE(storage.Value()->ResolveKey(key).Ok()) << "应拒绝: " << key;
    EXPECT_FALSE(storage.Value()->Exists(key));
  }
}

TEST(LocalObjectStorageTest, ResolvedPathStaysInsideRoot) {
  testing::TempDir dir("storage_inside");
  auto storage = LocalObjectStorage::Create(dir.Path());
  ASSERT_TRUE(storage.Ok());

  const auto key = MakeObjectKey(std::string(64, 'c'));
  ASSERT_TRUE(key.Ok());
  auto resolved = storage.Value()->ResolveKey(key.Value());
  ASSERT_TRUE(resolved.Ok());

  const std::string root = storage.Value()->root().generic_string();
  const std::string path = resolved.Value().generic_string();
  EXPECT_EQ(path.rfind(root, 0), 0U) << "解析结果必须位于存储根目录内";
}

TEST(LocalObjectStorageTest, ListsOnlyStaleTempFiles) {
  testing::TempDir dir("storage_stale_tmp");
  auto storage = LocalObjectStorage::Create(dir.Path());
  ASSERT_TRUE(storage.Ok()) << storage.GetError().message;

  // 一个"很久以前"的遗留临时文件
  const auto stale_key = std::string("tmp/leftover-uuid");
  auto stale_path = storage.Value()->ResolveKey(stale_key);
  ASSERT_TRUE(stale_path.Ok());
  {
    std::ofstream out(stale_path.Value(), std::ios::binary);
    out << "half merged file";
  }
  const auto old_time = std::filesystem::file_time_type::clock::now() - std::chrono::hours(48);
  std::error_code ec;
  std::filesystem::last_write_time(stale_path.Value(), old_time, ec);
  ASSERT_FALSE(ec) << "准备场景失败: " << ec.message();

  // 一个刚刚写入的临时文件（模拟正在进行的上传）
  auto writer = storage.Value()->CreateWriter();
  ASSERT_TRUE(writer.Ok());
  ASSERT_TRUE(writer.Value()->Write("x", 1).Ok());
  ASSERT_TRUE(writer.Value()->Finish().Ok());

  auto keys = storage.Value()->ListStaleTempKeys(24 * 60 * 60);
  ASSERT_TRUE(keys.Ok()) << keys.GetError().message;
  EXPECT_EQ(keys.Value().size(), 1U) << "只能列出超过年龄阈值的临时文件";
  EXPECT_EQ(keys.Value()[0], stale_key);

  // 阈值放宽到 0：新文件也会被列出（证明过滤确实按年龄生效）
  auto all = storage.Value()->ListStaleTempKeys(0);
  ASSERT_TRUE(all.Ok());
  EXPECT_EQ(all.Value().size(), 2U);
}

TEST(LocalObjectStorageTest, ReportsAvailableDiskSpace) {
  testing::TempDir dir("storage_space");
  auto storage = LocalObjectStorage::Create(dir.Path());
  ASSERT_TRUE(storage.Ok()) << storage.GetError().message;

  auto available = storage.Value()->AvailableBytes();
  ASSERT_TRUE(available.Ok()) << available.GetError().message;
  EXPECT_GT(available.Value(), 0U) << "可用空间必须是真实查询到的正数";
}

}  // namespace
}  // namespace lan_file_hub
