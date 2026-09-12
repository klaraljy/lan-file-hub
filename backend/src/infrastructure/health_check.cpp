#include "lan_file_hub/infrastructure/health_check.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <utility>

#ifdef LAN_FILE_HUB_HAVE_LIBPQXX
#include <pqxx/pqxx>
#endif

#ifdef LAN_FILE_HUB_HAVE_HIREDIS
#ifdef _WIN32
// Windows 上 timeval / socket 类型来自 winsock2.h；必须在 hiredis.h 之前引入
#include <winsock2.h>
#endif
#include <hiredis/hiredis.h>
#endif

namespace lan_file_hub {
namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t ElapsedMs(Clock::time_point start) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
}

std::mutex g_checker_mutex;
std::shared_ptr<const HealthChecker> g_checker;

// postgresql:// 或 keyword=value 两种形式都补上 connect_timeout
std::string BuildPostgresConnInfo(const DatabaseConfig& database) {
  const auto seconds = std::max<std::uint32_t>(1, database.connect_timeout_ms / 1000);
  const std::string timeout = "connect_timeout=" + std::to_string(seconds);
  std::string info = database.url;
  if (info.find("://") != std::string::npos) {
    info += (info.find('?') == std::string::npos) ? "?" : "&";
    info += timeout;
  } else {
    info += " ";
    info += timeout;
  }
  return info;
}

struct RedisEndpoint {
  std::string host{"127.0.0.1"};
  int port{6379};
  std::string password;
  int database{0};
};

// 支持 redis://[user:pass@]host[:port][/db] 与裸 host:port 两种写法。
std::optional<RedisEndpoint> ParseRedisUrl(const std::string& url) {
  if (url.empty()) {
    return std::nullopt;
  }

  RedisEndpoint endpoint;
  std::string rest = url;
  const auto scheme = rest.find("://");
  if (scheme != std::string::npos) {
    rest = rest.substr(scheme + 3);
  }

  const auto slash = rest.find('/');
  if (slash != std::string::npos) {
    const std::string db = rest.substr(slash + 1);
    rest = rest.substr(0, slash);
    if (!db.empty()) {
      try {
        endpoint.database = std::stoi(db);
      } catch (const std::exception&) {
        return std::nullopt;
      }
    }
  }

  const auto at = rest.rfind('@');
  if (at != std::string::npos) {
    const std::string userinfo = rest.substr(0, at);
    rest = rest.substr(at + 1);
    const auto colon = userinfo.find(':');
    if (colon != std::string::npos) {
      endpoint.password = userinfo.substr(colon + 1);
    }
  }

  const auto colon = rest.rfind(':');
  if (colon != std::string::npos) {
    endpoint.host = rest.substr(0, colon);
    try {
      endpoint.port = std::stoi(rest.substr(colon + 1));
    } catch (const std::exception&) {
      return std::nullopt;
    }
  } else {
    endpoint.host = rest;
  }

  if (endpoint.host.empty() || endpoint.port <= 0 || endpoint.port > 65535) {
    return std::nullopt;
  }
  return endpoint;
}

}  // namespace

HealthChecker::HealthChecker(DatabaseConfig database, RedisConfig redis, StorageConfig storage,
                             std::vector<std::string> secrets)
    : database_(std::move(database)),
      redis_(std::move(redis)),
      storage_(std::move(storage)),
      secrets_(std::move(secrets)) {}

std::string HealthChecker::Redact(std::string text) const {
  return RedactSecrets(std::move(text), secrets_);
}

ComponentHealth HealthChecker::CheckPostgres() const {
  ComponentHealth health{.name = "postgresql", .required = true, .ok = false, .detail = {}};
  const auto start = Clock::now();

  if (database_.url.empty()) {
    health.detail = "DATABASE_URL 未配置";
    health.latency_ms = ElapsedMs(start);
    return health;
  }

#ifdef LAN_FILE_HUB_HAVE_LIBPQXX
  try {
    pqxx::connection connection(BuildPostgresConnInfo(database_));
    if (!connection.is_open()) {
      health.detail = "连接未打开";
    } else {
      // 用真实事务而不是 nontransaction：后者在 libpqxx 各版本间存在差异
      pqxx::work tx(connection);
      const pqxx::result rows = tx.exec("SELECT 1");
      tx.commit();
      health.ok = !rows.empty();
      if (!health.ok) {
        health.detail = "SELECT 1 未返回结果";
      }
    }
  } catch (const std::exception& ex) {
    // 注意脱敏：libpq 的异常文本可能带上连接串
    health.detail = Redact(ex.what());
  }
#else
  health.detail = "构建时未启用 libpqxx，无法探测（见 README「已知限制」）";
#endif

  health.latency_ms = ElapsedMs(start);
  return health;
}

ComponentHealth HealthChecker::CheckRedis() const {
  ComponentHealth health{.name = "redis", .required = true, .ok = false, .detail = {}};
  const auto start = Clock::now();

  if (redis_.url.empty()) {
    health.detail = "REDIS_URL 未配置";
    health.latency_ms = ElapsedMs(start);
    return health;
  }

#ifdef LAN_FILE_HUB_HAVE_HIREDIS
  const auto endpoint = ParseRedisUrl(redis_.url);
  if (!endpoint.has_value()) {
    health.detail = "REDIS_URL 解析失败";
    health.latency_ms = ElapsedMs(start);
    return health;
  }

  const auto timeout_ms = static_cast<int>(std::max<std::uint32_t>(200, redis_.connect_timeout_ms));
  timeval timeout{};
  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;

  redisContext* context = redisConnectWithTimeout(endpoint->host.c_str(), endpoint->port, timeout);
  if (context == nullptr) {
    health.detail = "无法分配 Redis 连接";
  } else if (context->err != 0) {
    health.detail = context->errstr;
  } else {
    bool authenticated = true;
    if (!endpoint->password.empty()) {
      auto* auth =
          static_cast<redisReply*>(redisCommand(context, "AUTH %s", endpoint->password.c_str()));
      authenticated = auth != nullptr && auth->type != REDIS_REPLY_ERROR;
      if (auth != nullptr) {
        freeReplyObject(auth);
      }
      if (!authenticated) {
        health.detail = "AUTH 失败";
      }
    }
    if (authenticated && endpoint->database != 0) {
      auto* select =
          static_cast<redisReply*>(redisCommand(context, "SELECT %d", endpoint->database));
      if (select != nullptr) {
        freeReplyObject(select);
      }
    }
    if (authenticated) {
      auto* reply = static_cast<redisReply*>(redisCommand(context, "PING"));
      if (reply == nullptr) {
        health.detail = "PING 无响应";
      } else {
        health.ok = reply->type == REDIS_REPLY_STATUS && reply->str != nullptr &&
                    std::strcmp(reply->str, "PONG") == 0;
        if (!health.ok) {
          health.detail = "PING 返回异常";
        }
        freeReplyObject(reply);
      }
    }
  }
  if (context != nullptr) {
    redisFree(context);
  }
  health.detail = Redact(std::move(health.detail));
#else
  health.detail = "构建时未启用 hiredis，无法探测（见 README「已知限制」）";
#endif

  health.latency_ms = ElapsedMs(start);
  return health;
}

ComponentHealth HealthChecker::CheckObjectStorage() const {
  ComponentHealth health{.name = "object_storage", .required = true, .ok = false, .detail = {}};
  const auto start = Clock::now();

  std::error_code ec;
  if (!std::filesystem::is_directory(storage_.root, ec)) {
    health.detail = "存储根目录不存在或不是目录: " + storage_.root.string();
    health.latency_ms = ElapsedMs(start);
    return health;
  }

  // 只做一次极小的写入探测，验证可写后立即删除
  const auto probe = storage_.root / ".health-write-probe";
  {
    std::ofstream out(probe, std::ios::binary | std::ios::trunc);
    if (!out) {
      health.detail = "存储根目录不可写: " + storage_.root.string();
      health.latency_ms = ElapsedMs(start);
      return health;
    }
    out << "ok";
  }
  std::filesystem::remove(probe, ec);
  health.ok = true;
  health.latency_ms = ElapsedMs(start);
  return health;
}

HealthReport HealthChecker::CheckReadiness() const {
  HealthReport report;
  report.components.push_back(CheckPostgres());
  report.components.push_back(CheckRedis());
  report.components.push_back(CheckObjectStorage());

  report.ready = true;
  for (const auto& component : report.components) {
    if (component.required && !component.ok) {
      report.ready = false;
      break;
    }
  }
  return report;
}

void SetHealthChecker(std::shared_ptr<const HealthChecker> checker) {
  std::lock_guard<std::mutex> lock(g_checker_mutex);
  g_checker = std::move(checker);
}

std::shared_ptr<const HealthChecker> GetHealthChecker() {
  std::lock_guard<std::mutex> lock(g_checker_mutex);
  return g_checker;
}

}  // namespace lan_file_hub
