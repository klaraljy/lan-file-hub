// 健康检查：区分存活（live）与就绪（ready）。
//   live  —— 只表示进程还活着，永远不依赖外部组件
//   ready —— 必要依赖（PostgreSQL、Redis、对象存储根目录）可用
// 依据 docs/architecture.md 与 docs/phases/01-infrastructure.md。
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lan_file_hub/common/config.h"

namespace lan_file_hub {

struct ComponentHealth {
  std::string name;     // postgresql / redis / object_storage
  bool required{true};  // ready 判定是否受它影响
  bool ok{false};
  std::string detail;  // 失败原因；不得包含凭据（调用方负责脱敏）
  std::uint64_t latency_ms{0};
};

struct HealthReport {
  bool ready{false};
  std::vector<ComponentHealth> components;
};

class HealthChecker {
 public:
  HealthChecker(DatabaseConfig database, RedisConfig redis, StorageConfig storage,
                std::vector<std::string> secrets);

  // 逐个检查必要依赖。任何一个必要依赖不可用 => ready = false。
  // 任何单个依赖检查失败都不会抛出异常，也不会让进程退出。
  [[nodiscard]] HealthReport CheckReadiness() const;

  // 单项检查对外公开：既便于测试，也便于将来在管理端按组件展示。
  [[nodiscard]] ComponentHealth CheckPostgres() const;
  [[nodiscard]] ComponentHealth CheckRedis() const;
  [[nodiscard]] ComponentHealth CheckObjectStorage() const;

 private:
  [[nodiscard]] std::string Redact(std::string text) const;

  DatabaseConfig database_;
  RedisConfig redis_;
  StorageConfig storage_;
  std::vector<std::string> secrets_;
};

// 进程内共享的健康检查器：main 启动时注入，控制器读取。
// 未注入时 GetHealthChecker() 返回 nullptr，控制器按「未就绪」处理。
void SetHealthChecker(std::shared_ptr<const HealthChecker> checker);
[[nodiscard]] std::shared_ptr<const HealthChecker> GetHealthChecker();

}  // namespace lan_file_hub
