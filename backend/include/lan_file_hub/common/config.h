// 配置读取：集中在这里，业务代码不得直接读环境变量。
// 依据 docs/deployment.md「配置管理」：
//   - 提供 .env.example；真实凭据不进仓库
//   - 生产环境启动时检查必需配置；配置错误必须启动失败并说明原因
//   - 路径规范化为绝对路径；数值配置做上下限校验
//   - 本阶段没有任何支持热加载的配置项（改动一律需要重启）
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "lan_file_hub/common/result.h"

namespace lan_file_hub {

// 读取环境变量（跨平台：MSVC 用 _dupenv_s 规避 C4996，其余平台用 std::getenv）。
// 不存在返回 std::nullopt。**业务代码不要直接用它**：配置统一走 AppConfig；
// 只有配置加载、迁移工具与测试夹具需要读环境变量。
[[nodiscard]] std::optional<std::string> GetEnvVar(const char* name);
struct DatabaseConfig {
  std::string url;  // DATABASE_URL，postgresql:// 形式
  std::uint32_t connect_timeout_ms{3000};
};

struct RedisConfig {
  std::string url;  // REDIS_URL，redis:// 形式
  std::uint32_t connect_timeout_ms{2000};
};

struct StorageConfig {
  std::filesystem::path root;  // 已规范化为绝对路径
};

struct AppConfig {
  std::string app_env{"development"};
  std::uint16_t http_port{8080};
  std::uint32_t http_threads{4};
  std::string log_level{"info"};
  std::string log_file;  // 空 = 只输出到控制台

  // 前端静态文件目录（五阶段改造 ④：便携包单端口形态）。
  // 空 = 不托管前端（开发时用 vite，生产用 nginx 都走这条路）；
  // 非空 = 由后端直接托管前端 dist：静态资源走 documentRoot，前端路由回退到 index.html。
  // 这样便携包里**不需要 Node.js**，只有一个端口、一个进程。
  std::string web_root;

  DatabaseConfig database;
  RedisConfig redis;
  StorageConfig storage;

  std::uint64_t max_file_size_bytes{10ULL * 1024 * 1024 * 1024};  // 10 GiB
  std::uint64_t default_chunk_size_bytes{8ULL * 1024 * 1024};     // 8 MiB
  std::uint32_t max_concurrent_uploads{16};
  std::uint32_t max_concurrent_uploads_per_user{4};
  std::uint64_t max_user_storage_bytes{20ULL * 1024 * 1024 * 1024};  // 20 GiB
  std::uint64_t min_free_disk_bytes{1ULL * 1024 * 1024 * 1024};      // 保留 1 GiB
  // 限流（阶段十二）：窗口都是 1 分钟
  std::uint32_t login_rate_limit_per_minute{10};     // 同一 IP + 同一账号
  std::uint32_t login_ip_rate_limit_per_minute{60};  // 同一 IP（挡住换用户名撞库）
  std::uint32_t upload_rate_limit_per_minute{120};   // 同一用户创建上传会话
  std::uint32_t upload_session_ttl_seconds{24U * 60U * 60U};

  std::string jwt_secret;  // 生产环境必填；不得写入源码
  std::uint32_t jwt_expire_seconds{3600};

  std::uint64_t download_rate_limit_bytes_per_second{0};  // 0 = 不限速
  std::vector<std::string> cors_allowed_origins{"http://localhost:5173"};

  [[nodiscard]] bool IsProduction() const { return app_env == "production"; }
};

struct ConfigLoadResult {
  AppConfig config;
  // 已从内存中读取的所有敏感值（数据库口令、Redis 口令、JWT secret）。
  // 日志与错误信息在输出前必须把它们的出现替换掉，见 RedactSecrets()。
  std::vector<std::string> secrets;
};

// 从 .env（若存在）与进程环境变量读取配置并校验。
// 环境变量优先于 .env 中的同名项。
// 失败时返回 Error，调用方必须打印原因并以非零码退出。
[[nodiscard]] Result<ConfigLoadResult> LoadConfig(const std::filesystem::path& dotenv_path);

// 把 text 中出现的任何 secret 替换为 ***，用于日志与错误信息。
[[nodiscard]] std::string RedactSecrets(std::string text, const std::vector<std::string>& secrets);

// 解析 .env 内容为键值对（纯函数，便于单测）。
// 规则：忽略空行与 # 开头的注释；支持 KEY=VALUE 与 export KEY=VALUE；
// 值两端空白被裁剪；成对的单/双引号会被去掉；不支持的写法直接跳过。
[[nodiscard]] std::vector<std::pair<std::string, std::string>> ParseDotEnv(std::string_view text);

}  // namespace lan_file_hub
