#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fixtures/temp_dir.h"
#include "lan_file_hub/common/config.h"

namespace lan_file_hub {
namespace {

TEST(ParseDotEnvTest, SkipsCommentsAndBlankLines) {
  const auto entries = ParseDotEnv(
      "# 这是注释\n"
      "\n"
      "HTTP_PORT=8080\n"
      "   \n"
      "LOG_LEVEL=debug\n");
  ASSERT_EQ(entries.size(), 2U);
  EXPECT_EQ(entries[0].first, "HTTP_PORT");
  EXPECT_EQ(entries[0].second, "8080");
  EXPECT_EQ(entries[1].first, "LOG_LEVEL");
  EXPECT_EQ(entries[1].second, "debug");
}

TEST(ParseDotEnvTest, SupportsExportPrefixAndQuotes) {
  const auto entries = ParseDotEnv(
      "export APP_ENV=production\n"
      "JWT_SECRET=\"a b c\"\n"
      "STORAGE_ROOT='./data'\n");
  ASSERT_EQ(entries.size(), 3U);
  EXPECT_EQ(entries[0].first, "APP_ENV");
  EXPECT_EQ(entries[0].second, "production");
  EXPECT_EQ(entries[1].second, "a b c");
  EXPECT_EQ(entries[2].second, "./data");
}

TEST(ParseDotEnvTest, SkipsMalformedLines) {
  const auto entries = ParseDotEnv(
      "NO_EQUALS_SIGN\n"
      "=novalue\n"
      "OK=1\n");
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_EQ(entries[0].first, "OK");
}

TEST(RedactSecretsTest, ReplacesSecretsAndIgnoresShortOnes) {
  const std::string text = "连接 postgresql://user:hunter2@host/db 失败，code=ab";
  const std::string redacted = RedactSecrets(text, {"hunter2", "ab"});
  EXPECT_EQ(redacted.find("hunter2"), std::string::npos);
  EXPECT_NE(redacted.find("***"), std::string::npos);
  // 长度小于 4 的串不替换，避免把正常文本打烂
  EXPECT_NE(redacted.find("code=ab"), std::string::npos);
}

TEST(LoadConfigTest, UsesDocumentedDefaultsWhenNothingIsConfigured) {
  testing::TempDir dir("config_defaults");
  const auto loaded = LoadConfig(dir.File("missing.env"));
  ASSERT_TRUE(loaded.Ok()) << loaded.GetError().message;
  const AppConfig& config = loaded.Value().config;
  EXPECT_EQ(config.app_env, "development");
  EXPECT_EQ(config.http_port, 8080);
  EXPECT_EQ(config.default_chunk_size_bytes, 8ULL * 1024 * 1024);
  EXPECT_EQ(config.max_file_size_bytes, 10ULL * 1024 * 1024 * 1024);
  EXPECT_EQ(config.max_user_storage_bytes, 20ULL * 1024 * 1024 * 1024);
  EXPECT_FALSE(config.IsProduction());
  EXPECT_TRUE(config.storage.root.is_absolute());
}

TEST(LoadConfigTest, ReadsValuesFromDotEnvFile) {
  testing::TempDir dir("config_dotenv");
  dir.WriteFile("test.env",
                "HTTP_PORT=9100\n"
                "LOG_LEVEL=debug\n"
                "DEFAULT_CHUNK_SIZE=4194304\n"
                "CORS_ALLOWED_ORIGINS=http://localhost:5173, http://127.0.0.1:5173\n");
  const auto loaded = LoadConfig(dir.File("test.env"));
  ASSERT_TRUE(loaded.Ok()) << loaded.GetError().message;
  const AppConfig& config = loaded.Value().config;
  EXPECT_EQ(config.http_port, 9100);
  EXPECT_EQ(config.log_level, "debug");
  EXPECT_EQ(config.default_chunk_size_bytes, 4194304U);
  ASSERT_EQ(config.cors_allowed_origins.size(), 2U);
  EXPECT_EQ(config.cors_allowed_origins[1], "http://127.0.0.1:5173");
}

// WEB_ROOT：便携包单端口形态（五阶段改造 ④）。
// 默认必须为空 —— 否则开发环境下后端会去托管不存在的前端目录。
TEST(LoadConfigTest, WebRootIsEmptyByDefaultAndReadFromEnv) {
  testing::TempDir dir("config_webroot");
  const auto defaults = LoadConfig(dir.File("missing.env"));
  ASSERT_TRUE(defaults.Ok()) << defaults.GetError().message;
  EXPECT_TRUE(defaults.Value().config.web_root.empty())
      << "默认不能托管前端：开发时前端由 vite 托管";

  dir.WriteFile("web.env", "WEB_ROOT=web\n");
  const auto loaded = LoadConfig(dir.File("web.env"));
  ASSERT_TRUE(loaded.Ok()) << loaded.GetError().message;
  EXPECT_EQ(loaded.Value().config.web_root, "web");
}

// 配套安卓安装包的位置（用户 2026-10-03）：显式 APP_APK_PATH 优先；
// 没配就跟着 WEB_ROOT 走（便携包 = app\web\downloads\lan-file-hub.apk）；两者都没有 = 空。
TEST(LoadConfigTest, AppApkPathFollowsWebRootUnlessOverridden) {
  testing::TempDir dir("config_apk");

  const auto unspecified = LoadConfig(dir.File("missing.env"));
  ASSERT_TRUE(unspecified.Ok()) << unspecified.GetError().message;
  EXPECT_TRUE(unspecified.Value().config.app_apk_path.empty());

  dir.WriteFile("web.env", "WEB_ROOT=web\n");
  const auto by_web_root = LoadConfig(dir.File("web.env"));
  ASSERT_TRUE(by_web_root.Ok()) << by_web_root.GetError().message;
  EXPECT_EQ(
      by_web_root.Value().config.app_apk_path,
      (std::filesystem::absolute("web") / "downloads" / "lan-file-hub.apk").lexically_normal());

  dir.WriteFile("apk.env",
                "WEB_ROOT=web\n"
                "APP_APK_PATH=D:/somewhere/else/lan-file-hub.apk\n");
  const auto explicit_path = LoadConfig(dir.File("apk.env"));
  ASSERT_TRUE(explicit_path.Ok()) << explicit_path.GetError().message;
  EXPECT_TRUE(explicit_path.Value().config.app_apk_path.is_absolute());
  EXPECT_EQ(explicit_path.Value().config.app_apk_path.filename().string(), "lan-file-hub.apk");
  EXPECT_EQ(explicit_path.Value().config.app_apk_path.parent_path().filename().string(), "else");
}

TEST(LoadConfigTest, RejectsOutOfRangePort) {
  testing::TempDir dir("config_port");
  dir.WriteFile("test.env", "HTTP_PORT=99999\n");
  const auto loaded = LoadConfig(dir.File("test.env"));
  ASSERT_FALSE(loaded.Ok());
  EXPECT_EQ(loaded.GetError().code, "INVALID_CONFIGURATION");
  EXPECT_NE(loaded.GetError().message.find("HTTP_PORT"), std::string::npos);
}

TEST(LoadConfigTest, RejectsNonNumericValue) {
  testing::TempDir dir("config_nan");
  dir.WriteFile("test.env", "HTTP_PORT=abc\n");
  const auto loaded = LoadConfig(dir.File("test.env"));
  ASSERT_FALSE(loaded.Ok());
  EXPECT_EQ(loaded.GetError().code, "INVALID_CONFIGURATION");
}

TEST(LoadConfigTest, RejectsChunkSizeBelowProtocolMinimum) {
  testing::TempDir dir("config_chunk");
  dir.WriteFile("test.env", "DEFAULT_CHUNK_SIZE=1024\n");  // 小于 1 MiB
  const auto loaded = LoadConfig(dir.File("test.env"));
  ASSERT_FALSE(loaded.Ok());
  EXPECT_NE(loaded.GetError().message.find("DEFAULT_CHUNK_SIZE"), std::string::npos);
}

TEST(LoadConfigTest, RejectsUnknownLogLevel) {
  testing::TempDir dir("config_log");
  dir.WriteFile("test.env", "LOG_LEVEL=verbose\n");
  const auto loaded = LoadConfig(dir.File("test.env"));
  ASSERT_FALSE(loaded.Ok());
  EXPECT_NE(loaded.GetError().message.find("LOG_LEVEL"), std::string::npos);
}

TEST(LoadConfigTest, RejectsWildcardCors) {
  testing::TempDir dir("config_cors");
  dir.WriteFile("test.env", "CORS_ALLOWED_ORIGINS=*\n");
  const auto loaded = LoadConfig(dir.File("test.env"));
  ASSERT_FALSE(loaded.Ok());
  EXPECT_NE(loaded.GetError().message.find("CORS_ALLOWED_ORIGINS"), std::string::npos);
}

TEST(LoadConfigTest, ProductionRequiresStrongJwtSecret) {
  testing::TempDir dir("config_jwt");
  dir.WriteFile("test.env",
                "APP_ENV=production\n"
                "JWT_SECRET=too-short\n");
  const auto loaded = LoadConfig(dir.File("test.env"));
  ASSERT_FALSE(loaded.Ok());
  EXPECT_NE(loaded.GetError().message.find("JWT_SECRET"), std::string::npos);
}

TEST(LoadConfigTest, ProductionRejectsDevelopmentDatabasePassword) {
  testing::TempDir dir("config_prod_db");
  dir.WriteFile("test.env",
                "APP_ENV=production\n"
                "JWT_SECRET=0123456789abcdef0123456789abcdef\n"
                "DATABASE_URL=postgresql://lan_file_hub:lan_file_hub_dev@db:5432/lan_file_hub\n");
  const auto loaded = LoadConfig(dir.File("test.env"));
  ASSERT_FALSE(loaded.Ok());
  EXPECT_NE(loaded.GetError().message.find("开发默认数据库口令"), std::string::npos);
}

TEST(LoadConfigTest, CollectsSecretsForRedaction) {
  testing::TempDir dir("config_secrets");
  dir.WriteFile("test.env",
                "DATABASE_URL=postgresql://user:dbpassword@127.0.0.1:5432/db\n"
                "REDIS_URL=redis://:redispassword@127.0.0.1:6379/0\n"
                "JWT_SECRET=a-very-long-development-secret-value\n");
  const auto loaded = LoadConfig(dir.File("test.env"));
  ASSERT_TRUE(loaded.Ok()) << loaded.GetError().message;
  const auto& secrets = loaded.Value().secrets;
  EXPECT_NE(std::ranges::find(secrets, "dbpassword"), secrets.end());
  EXPECT_NE(std::ranges::find(secrets, "redispassword"), secrets.end());
  EXPECT_NE(std::ranges::find(secrets, "a-very-long-development-secret-value"), secrets.end());
}

}  // namespace
}  // namespace lan_file_hub
