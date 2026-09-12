#include <filesystem>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

#include "fixtures/temp_dir.h"
#include "lan_file_hub/infrastructure/health_check.h"

namespace lan_file_hub {
namespace {

DatabaseConfig UnreachableDatabase() {
  // 指向一个几乎不可能有服务监听的端口，且超时设到最短，避免测试变慢
  DatabaseConfig config;
  config.url = "postgresql://healthcheck:healthcheck@127.0.0.1:1/none";
  config.connect_timeout_ms = 200;
  return config;
}

RedisConfig UnreachableRedis() {
  RedisConfig config;
  config.url = "redis://127.0.0.1:1";
  config.connect_timeout_ms = 200;
  return config;
}

const ComponentHealth* FindComponent(const HealthReport& report, const std::string& name) {
  for (const auto& component : report.components) {
    if (component.name == name) {
      return &component;
    }
  }
  return nullptr;
}

TEST(HealthCheckTest, ObjectStorageIsOkForWritableDirectory) {
  testing::TempDir dir("health_ok");
  HealthChecker checker(DatabaseConfig{}, RedisConfig{}, StorageConfig{dir.Path()}, {});

  const ComponentHealth health = checker.CheckObjectStorage();
  EXPECT_TRUE(health.ok);
  EXPECT_EQ(health.name, "object_storage");
  EXPECT_TRUE(health.detail.empty());

  // 探测文件必须被清理掉，不能留在存储根目录里
  EXPECT_FALSE(std::filesystem::exists(dir.Path() / ".health-write-probe"));
}

TEST(HealthCheckTest, ObjectStorageFailsForMissingDirectory) {
  const auto missing = std::filesystem::temp_directory_path() / "lan_file_hub_missing_storage_root";
  std::error_code ec;
  std::filesystem::remove_all(missing, ec);

  HealthChecker checker(DatabaseConfig{}, RedisConfig{}, StorageConfig{missing}, {});
  const ComponentHealth health = checker.CheckObjectStorage();
  EXPECT_FALSE(health.ok);
  EXPECT_FALSE(health.detail.empty());
}

TEST(HealthCheckTest, ReadinessIsFalseWhenRequiredDependenciesAreDown) {
  testing::TempDir dir("health_ready");
  HealthChecker checker(UnreachableDatabase(), UnreachableRedis(), StorageConfig{dir.Path()}, {});

  const HealthReport report = checker.CheckReadiness();

  EXPECT_FALSE(report.ready) << "依赖不可用时绝不能声称就绪";
  ASSERT_EQ(report.components.size(), 3U);

  const ComponentHealth* storage = FindComponent(report, "object_storage");
  ASSERT_NE(storage, nullptr);
  EXPECT_TRUE(storage->ok);

  const ComponentHealth* postgres = FindComponent(report, "postgresql");
  ASSERT_NE(postgres, nullptr);
  EXPECT_FALSE(postgres->ok);

  const ComponentHealth* redis = FindComponent(report, "redis");
  ASSERT_NE(redis, nullptr);
  EXPECT_FALSE(redis->ok);
}

TEST(HealthCheckTest, EmptyUrlsAreReportedAsNotConfiguredInsteadOfCrashing) {
  testing::TempDir dir("health_empty");
  HealthChecker checker(DatabaseConfig{}, RedisConfig{}, StorageConfig{dir.Path()}, {});

  const ComponentHealth postgres = checker.CheckPostgres();
  EXPECT_FALSE(postgres.ok);
  EXPECT_NE(postgres.detail.find("DATABASE_URL"), std::string::npos);

  const ComponentHealth redis = checker.CheckRedis();
  EXPECT_FALSE(redis.ok);
  EXPECT_NE(redis.detail.find("REDIS_URL"), std::string::npos);
}

TEST(HealthCheckTest, SecretsNeverAppearInComponentDetail) {
  testing::TempDir dir("health_secret");

  DatabaseConfig database;
  database.url = "postgresql://user:supersecretvalue@127.0.0.1:1/db";
  database.connect_timeout_ms = 200;

  RedisConfig redis;
  redis.url = "redis://:anothersecret@127.0.0.1:1";
  redis.connect_timeout_ms = 200;

  HealthChecker checker(database, redis, StorageConfig{dir.Path()},
                        {"supersecretvalue", "anothersecret"});

  const ComponentHealth postgres = checker.CheckPostgres();
  EXPECT_EQ(postgres.detail.find("supersecretvalue"), std::string::npos)
      << "数据库口令不得出现在健康检查输出里";

  const ComponentHealth redis_health = checker.CheckRedis();
  EXPECT_EQ(redis_health.detail.find("anothersecret"), std::string::npos)
      << "Redis 口令不得出现在健康检查输出里";
}

TEST(HealthCheckTest, CheckerRegistryReturnsWhatWasInjected) {
  EXPECT_EQ(GetHealthChecker(), nullptr);

  auto checker = std::make_shared<HealthChecker>(
      DatabaseConfig{}, RedisConfig(), StorageConfig{std::filesystem::temp_directory_path()},
      std::vector<std::string>{});
  SetHealthChecker(checker);
  EXPECT_EQ(GetHealthChecker(), checker);

  SetHealthChecker(nullptr);
  EXPECT_EQ(GetHealthChecker(), nullptr);
}

}  // namespace
}  // namespace lan_file_hub
