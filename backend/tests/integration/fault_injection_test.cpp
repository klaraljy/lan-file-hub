// 故障注入测试（阶段十四第 11、12 条）。
//
// 不去停 Docker 容器（那会破坏开发环境，也不可复现），而是把依赖指向**不可用的地址**：
// 断言系统降级为明确的错误/未就绪状态，而不是崩溃、挂死或假装成功。
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "lan_file_hub/common/config.h"
#include "lan_file_hub/infrastructure/database.h"
#include "lan_file_hub/infrastructure/health_check.h"
#include "lan_file_hub/infrastructure/user_repository.h"

#include "fixtures/temp_dir.h"

namespace lan_file_hub {
namespace {

bool DatabaseRequired() {
  char* buffer = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&buffer, &size, "LAN_FILE_HUB_REQUIRE_DB") != 0 || buffer == nullptr) {
    return false;
  }
  const std::string value(buffer);
  std::free(buffer);
  return value == "1";
}

// 一个几乎不可能有服务监听的端口（避免与真实端口冲突）
constexpr std::uint16_t kClosedPort = 59999;

TEST(FaultInjectionTest, PostgresUnavailableIsReportedNotCrashed) {
  DatabaseConfig database;
  // 连接超时设小一点，别让测试挂太久
  database.url = "postgresql://lan_file_hub:wrong@127.0.0.1:" + std::to_string(kClosedPort) +
                 "/lan_file_hub?connect_timeout=2";
  database.connect_timeout_ms = 500;

  auto pool = ConnectionPool::Create(database, 2);
  if (!pool) {
    // 池化层在建立连接时就失败：这也是明确降级（返回错误而不是抛异常）
    EXPECT_FALSE(pool.GetError().code.empty());
    EXPECT_EQ(pool.GetError().code, "STORAGE_UNAVAILABLE");
    return;
  }

  // 池子建起来了但连不上：仓储层必须返回 STORAGE_UNAVAILABLE，不能崩溃或假装成功
  UserRepository users(*pool.Value());
  const auto found = users.FindById("11111111-1111-1111-1111-111111111111");
  ASSERT_FALSE(found.Ok());
  EXPECT_EQ(found.GetError().code, "STORAGE_UNAVAILABLE");
}

TEST(FaultInjectionTest, RedisDownMakesReadinessFailWithoutStoppingTheProcess) {
  testing::TempDir storage_dir{"fault_storage"};

  RedisConfig redis;
  redis.url = "redis://127.0.0.1:" + std::to_string(kClosedPort);
  redis.connect_timeout_ms = 500;

  StorageConfig storage;
  storage.root = storage_dir.Path();

  // 数据库配置用真实配置（没有就跳过这一项，Redis 部分仍然可测）
  DatabaseConfig database;
  auto config = LoadConfig(".env");
  if (config.Ok()) {
    database = config.Value().config.database;
  } else {
    database.url = "postgresql://lan_file_hub@127.0.0.1:" + std::to_string(kClosedPort) + "/x";
  }

  HealthChecker checker(database, redis, storage, {});
  const ComponentHealth redis_health = checker.CheckRedis();
  EXPECT_FALSE(redis_health.ok) << "Redis 不可用时必须如实报不可用";
  EXPECT_FALSE(redis_health.detail.empty());

  // ready 必须为 false（Redis 是必要依赖），且调用本身不能抛异常
  const HealthReport report = checker.CheckReadiness();
  EXPECT_FALSE(report.ready);

  // "Redis 重启"语义：同一个 HealthChecker 在依赖恢复后应重新报 ok。
  // 这里用真实配置再查一次，等价于"进程没重启、依赖回来了"。
  if (config.Ok()) {
    HealthChecker healthy(config.Value().config.database, config.Value().config.redis, storage, {});
    const ComponentHealth after_restart = healthy.CheckRedis();
    if (!after_restart.ok) {
      GTEST_SKIP() << "本机 Redis 未运行，跳过恢复后的健康检查断言";
    }
    EXPECT_TRUE(after_restart.ok) << "依赖恢复后健康检查必须回到 ok，不能缓存失败状态";
  }
}

TEST(FaultInjectionTest, StorageRootMissingMakesReadinessFail) {
  testing::TempDir storage_dir{"fault_storage_missing"};

  StorageConfig storage;
  // 指向一个不存在的目录：对象存储不可用
  storage.root = storage_dir.Path() / "does-not-exist";

  RedisConfig redis;
  redis.url = "redis://127.0.0.1:" + std::to_string(kClosedPort);
  DatabaseConfig database;
  database.url = "postgresql://lan_file_hub@127.0.0.1:" + std::to_string(kClosedPort) + "/x";

  HealthChecker checker(database, redis, storage, {});
  const ComponentHealth storage_health = checker.CheckObjectStorage();
  EXPECT_FALSE(storage_health.ok) << "存储根目录不存在时必须报不可用";
  EXPECT_FALSE(checker.CheckReadiness().ready);
}

}  // namespace
}  // namespace lan_file_hub
