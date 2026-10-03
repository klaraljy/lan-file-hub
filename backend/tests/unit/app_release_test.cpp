// 「配套手机应用」安装包信息（用户 2026-10-03：网页「我的」里的安卓下载入口）。
//
// 这里钉住三件容易做错的事：
//   ① 大小/时间**以真实文件为准** —— 侧车里写错了、或者只放了 APK 没放侧车，接口也不能给出假数字；
//   ② 文件被替换之后（= 重新构建 → 构建脚本同步新包）立刻给出新值，**不需要重启后端**；
//   ③ 没有安装包时是"如实说没有"（available=false），而不是抛异常 / 500。
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "fixtures/temp_dir.h"
#include "lan_file_hub/application/app_release.h"
#include "lan_file_hub/common/sha256.h"

namespace lan_file_hub {
namespace {

constexpr char kApkName[] = "lan-file-hub-share.apk";
constexpr char kPayload[] = "APK-BYTES-0123456789";

TEST(AppReleaseTest, ReportsUnavailableWhenApkIsMissing) {
  testing::TempDir dir("app_release_missing");
  AppRelease release(dir.File(kApkName));

  const AppReleaseInfo info = release.Describe();
  EXPECT_FALSE(info.available);
  EXPECT_EQ(info.size_bytes, 0U);
  EXPECT_TRUE(info.built_at.empty());
  EXPECT_FALSE(release.ResolvePath().has_value());
}

TEST(AppReleaseTest, UsesSidecarForVersionButAlwaysHashesTheRealFile) {
  testing::TempDir dir("app_release_manifest");
  dir.WriteFile(kApkName, kPayload);
  dir.WriteFile(std::string(kApkName) + ".json",
                R"({
                  "version_name": "1.2",
                  "version_code": 12,
                  "built_at": "2026-10-03T09:01:18Z",
                  "sha256": "deadbeef",
                  "size_bytes": )" +
                    std::to_string(sizeof(kPayload) - 1) + R"(
                })");

  AppRelease release(dir.File(kApkName));
  const AppReleaseInfo info = release.Describe();
  ASSERT_TRUE(info.available);
  EXPECT_EQ(info.file_name, kApkName);
  EXPECT_EQ(info.version_name, "1.2");
  EXPECT_EQ(info.version_code, 12U);
  EXPECT_EQ(info.built_at, "2026-10-03T09:01:18Z");
  // 侧车里的 sha256 是错的（"deadbeef"）：必须现算真实文件的，不能照抄
  EXPECT_EQ(info.sha256, Sha256Hex(kPayload));
  EXPECT_EQ(info.size_bytes, sizeof(kPayload) - 1);
  EXPECT_EQ(info.download_path, "/api/v1/app/download");
  ASSERT_TRUE(release.ResolvePath().has_value());
}

// 实测踩到过的坑（2026-10-03 受控实验）：只把新包放到下载目录、侧车还是旧的，
// 结果卡片显示"新的大小 + 旧的时间"，自相矛盾。规则：侧车与包对不上就整体忽略它。
TEST(AppReleaseTest, IgnoresStaleSidecarSoNumbersNeverContradict) {
  testing::TempDir dir("app_release_stale_manifest");
  dir.WriteFile(kApkName, kPayload);
  dir.WriteFile(std::string(kApkName) + ".json",
                R"({"version_name":"1.2","version_code":12,"built_at":"2026-10-03T09:01:18Z",
                    "size_bytes": 999999})");

  AppRelease release(dir.File(kApkName));
  const AppReleaseInfo info = release.Describe();
  ASSERT_TRUE(info.available);
  // 侧车说的包不是这个包 → 版本号空着、时间用文件时间（不再是侧车里的旧时间）
  EXPECT_TRUE(info.version_name.empty());
  EXPECT_NE(info.built_at, "2026-10-03T09:01:18Z");
  EXPECT_EQ(info.size_bytes, sizeof(kPayload) - 1);
}

TEST(AppReleaseTest, FallsBackToFileStampWhenSidecarIsAbsent) {
  testing::TempDir dir("app_release_no_manifest");
  dir.WriteFile(kApkName, kPayload);

  AppRelease release(dir.File(kApkName));
  const AppReleaseInfo info = release.Describe();
  ASSERT_TRUE(info.available);
  EXPECT_EQ(info.size_bytes, sizeof(kPayload) - 1);
  EXPECT_TRUE(info.version_name.empty());
  EXPECT_EQ(info.version_code, 0U);
  // 没有侧车也要能算出校验和与时间（时间退化成文件时间，不能是空串）
  EXPECT_EQ(info.sha256, Sha256Hex(kPayload));
  EXPECT_FALSE(info.built_at.empty());
  EXPECT_EQ(info.built_at.back(), 'Z');
}

TEST(AppReleaseTest, SurvivesBrokenSidecar) {
  testing::TempDir dir("app_release_broken_manifest");
  dir.WriteFile(kApkName, kPayload);
  dir.WriteFile(std::string(kApkName) + ".json", "{ this is not json");

  AppRelease release(dir.File(kApkName));
  const AppReleaseInfo info = release.Describe();
  ASSERT_TRUE(info.available);
  EXPECT_EQ(info.size_bytes, sizeof(kPayload) - 1);
  EXPECT_EQ(info.sha256, Sha256Hex(kPayload));
}

TEST(AppReleaseTest, PicksUpRebuiltApkWithoutRestart) {
  testing::TempDir dir("app_release_rebuild");
  dir.WriteFile(kApkName, kPayload);
  AppRelease release(dir.File(kApkName));

  const AppReleaseInfo before = release.Describe();
  ASSERT_TRUE(before.available);
  const std::uint64_t first_size = before.size_bytes;

  // 重新构建：包变大 + 侧车换成新版本（构建脚本同步新包时就是这个样子，大小一定对得上）
  const std::string bigger(static_cast<std::size_t>(first_size) + 1024, 'x');
  dir.WriteFile(kApkName, bigger);
  dir.WriteFile(std::string(kApkName) + ".json",
                std::string(R"({"version_name":"1.3","version_code":13,)"
                            R"("built_at":"2026-10-03T10:00:00Z","size_bytes":)") +
                    std::to_string(bigger.size()) + "}");

  const AppReleaseInfo after = release.Describe();
  ASSERT_TRUE(after.available);
  EXPECT_EQ(after.size_bytes, first_size + 1024);
  EXPECT_EQ(after.version_name, "1.3");
  EXPECT_EQ(after.built_at, "2026-10-03T10:00:00Z");
  EXPECT_NE(after.sha256, before.sha256);
}

TEST(AppReleaseTest, EmptyConfiguredPathIsNotAnError) {
  AppRelease release{std::filesystem::path{}};
  const AppReleaseInfo info = release.Describe();
  EXPECT_FALSE(info.available);
  EXPECT_FALSE(release.ResolvePath().has_value());
}

}  // namespace
}  // namespace lan_file_hub
