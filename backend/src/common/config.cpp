#include "lan_file_hub/common/config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace lan_file_hub {
namespace {

// 开发环境的默认连接串。口令仅用于本机 docker-compose，生产环境会被明确拒绝（见 Validate）。
constexpr std::string_view kDevDatabaseUrl =
    "postgresql://lan_file_hub:lan_file_hub_dev@127.0.0.1:5432/lan_file_hub";
constexpr std::string_view kDevDatabasePassword = "lan_file_hub_dev";
constexpr std::string_view kDevRedisUrl = "redis://127.0.0.1:6379/0";

constexpr std::uint64_t kMinChunkSize = 1ULL * 1024 * 1024;   // 1 MiB（docs/storage.md）
constexpr std::uint64_t kMaxChunkSize = 64ULL * 1024 * 1024;  // 64 MiB
constexpr std::uint64_t kMaxAllowedFileSize = 1ULL << 40;     // 1 TiB 上限，防止误配

std::string Trim(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

std::string ToLower(std::string text) {
  std::ranges::transform(text, text.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

std::optional<std::string> GetEnv(const char* name) {
  // 全项目唯一允许直接读环境变量的地方（AGENTS.md §5：业务代码不得直接读环境变量）
#if defined(_MSC_VER)
  // MSVC 把 std::getenv 标记为不安全（C4996），在 /WX 下会直接编译失败
  char* buffer = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) {
    return std::nullopt;
  }
  std::string value(buffer);
  std::free(buffer);
  return value;
#else
  const char* raw = std::getenv(name);
  if (raw == nullptr)
    return std::nullopt;
  return std::string(raw);
#endif
}

Error ConfigError(std::string message, std::string detail = {}) {
  return Error{"INVALID_CONFIGURATION", std::move(message), std::move(detail)};
}

// 取字符串配置：环境变量优先，其次 .env，最后默认值。
std::string PickString(const std::unordered_map<std::string, std::string>& dotenv, const char* name,
                       std::string fallback) {
  if (auto env = GetEnv(name); env.has_value()) {
    return *env;
  }
  auto it = dotenv.find(name);
  if (it != dotenv.end()) {
    return it->second;
  }
  return fallback;
}

Result<std::uint64_t> PickUint(const std::unordered_map<std::string, std::string>& dotenv,
                               const char* name, std::uint64_t fallback, std::uint64_t min_value,
                               std::uint64_t max_value) {
  std::string raw = PickString(dotenv, name, std::to_string(fallback));
  const std::string trimmed = Trim(raw);
  if (trimmed.empty()) {
    return ConfigError(std::string(name) + " 不能为空");
  }
  std::uint64_t value = 0;
  for (const char c : trimmed) {
    if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
      return ConfigError(std::string(name) + " 必须是十进制非负整数", "实际值: " + trimmed);
    }
    const auto digit = static_cast<std::uint64_t>(c - '0');
    if (value > (max_value - digit) / 10ULL) {
      return ConfigError(std::string(name) + " 超出允许范围", "实际值: " + trimmed);
    }
    value = value * 10ULL + digit;
  }
  if (value < min_value || value > max_value) {
    return ConfigError(std::string(name) + " 超出允许范围",
                       "允许区间 [" + std::to_string(min_value) + ", " + std::to_string(max_value) +
                           "]，实际值: " + trimmed);
  }
  return value;
}

std::vector<std::string> SplitList(const std::string& raw) {
  std::vector<std::string> items;
  std::stringstream stream(raw);
  std::string item;
  while (std::getline(stream, item, ',')) {
    std::string trimmed = Trim(item);
    if (!trimmed.empty()) {
      items.push_back(std::move(trimmed));
    }
  }
  return items;
}

// 从 URL 中取出口令，用于日志脱敏与生产环境的弱口令检查。
std::optional<std::string> ExtractPassword(std::string_view url) {
  const auto scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) {
    return std::nullopt;
  }
  const auto at = url.find('@', scheme_end + 3);
  if (at == std::string_view::npos) {
    return std::nullopt;
  }
  const auto authority = url.substr(scheme_end + 3, at - scheme_end - 3);
  const auto colon = authority.find(':');
  if (colon == std::string_view::npos) {
    return std::nullopt;
  }
  return std::string(authority.substr(colon + 1));
}

bool IsKnownLogLevel(const std::string& level) {
  static const std::vector<std::string> kLevels{"trace", "debug",    "info", "warn",
                                                "error", "critical", "off"};
  return std::ranges::find(kLevels, level) != kLevels.end();
}

}  // namespace

std::vector<std::pair<std::string, std::string>> ParseDotEnv(std::string_view text) {
  std::vector<std::pair<std::string, std::string>> entries;
  std::size_t pos = 0;
  while (pos <= text.size()) {
    std::size_t line_end = text.find('\n', pos);
    if (line_end == std::string_view::npos) {
      line_end = text.size();
    }
    std::string line = Trim(text.substr(pos, line_end - pos));
    pos = line_end + 1;

    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (line.starts_with("export ")) {
      line = Trim(std::string_view(line).substr(7));
      if (line.empty()) {
        continue;
      }
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }

    std::string key = Trim(std::string_view(line).substr(0, eq));
    std::string value = Trim(std::string_view(line).substr(eq + 1));
    if (key.empty()) {
      continue;
    }

    // 去成对引号；不配对的引号按原样保留，不做聪明猜测
    if (value.size() >= 2) {
      const char first = value.front();
      if ((first == '"' || first == '\'') && value.back() == first) {
        value = value.substr(1, value.size() - 2);
      }
    }
    entries.emplace_back(std::move(key), std::move(value));
  }
  return entries;
}

std::string RedactSecrets(std::string text, const std::vector<std::string>& secrets) {
  for (const auto& secret : secrets) {
    if (secret.size() < 4) {
      continue;  // 太短的串替换会把正常文本打烂
    }
    std::size_t at = 0;
    while ((at = text.find(secret, at)) != std::string::npos) {
      text.replace(at, secret.size(), "***");
      at += 3;
    }
  }
  return text;
}

std::optional<std::string> GetEnvVar(const char* name) {
  return GetEnv(name);
}
Result<ConfigLoadResult> LoadConfig(const std::filesystem::path& dotenv_path) {
  std::unordered_map<std::string, std::string> dotenv;

  std::error_code ec;
  if (!dotenv_path.empty() && std::filesystem::exists(dotenv_path, ec) && !ec) {
    std::ifstream input(dotenv_path, std::ios::binary);
    if (!input) {
      return ConfigError("无法读取 .env 文件", dotenv_path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    for (auto& [key, value] : ParseDotEnv(buffer.str())) {
      dotenv[key] = std::move(value);
    }
  }

  AppConfig config;
  ConfigLoadResult result;

  config.app_env = ToLower(Trim(PickString(dotenv, "APP_ENV", "development")));
  config.log_level = ToLower(Trim(PickString(dotenv, "LOG_LEVEL", "info")));
  config.log_file = Trim(PickString(dotenv, "LOG_FILE", ""));

  if (!IsKnownLogLevel(config.log_level)) {
    return ConfigError(
        "LOG_LEVEL 取值不合法",
        "允许 trace/debug/info/warn/error/critical/off，实际值: " + config.log_level);
  }

  auto port = PickUint(dotenv, "HTTP_PORT", 8080, 1, 65535);
  if (!port) {
    return port.GetError();
  }
  config.http_port = static_cast<std::uint16_t>(port.Value());

  auto threads = PickUint(dotenv, "HTTP_THREADS", 4, 1, 256);
  if (!threads) {
    return threads.GetError();
  }
  config.http_threads = static_cast<std::uint32_t>(threads.Value());

  config.database.url = Trim(PickString(dotenv, "DATABASE_URL", std::string(kDevDatabaseUrl)));
  auto db_timeout = PickUint(dotenv, "DATABASE_CONNECT_TIMEOUT_MS", 3000, 200, 60000);
  if (!db_timeout) {
    return db_timeout.GetError();
  }
  config.database.connect_timeout_ms = static_cast<std::uint32_t>(db_timeout.Value());

  config.redis.url = Trim(PickString(dotenv, "REDIS_URL", std::string(kDevRedisUrl)));
  auto redis_timeout = PickUint(dotenv, "REDIS_CONNECT_TIMEOUT_MS", 2000, 200, 60000);
  if (!redis_timeout) {
    return redis_timeout.GetError();
  }
  config.redis.connect_timeout_ms = static_cast<std::uint32_t>(redis_timeout.Value());

  const std::string storage_root = Trim(PickString(dotenv, "STORAGE_ROOT", "./storage"));
  if (storage_root.empty()) {
    return ConfigError("STORAGE_ROOT 不能为空");
  }
  // 规范化为绝对路径（不解析符号链接，只做词法规范化）
  config.storage.root = std::filesystem::absolute(storage_root, ec).lexically_normal();
  if (ec) {
    return ConfigError("STORAGE_ROOT 无法规范化为绝对路径", storage_root);
  }

  auto max_file =
      PickUint(dotenv, "MAX_FILE_SIZE", 10ULL * 1024 * 1024 * 1024, 1, kMaxAllowedFileSize);
  if (!max_file) {
    return max_file.GetError();
  }
  config.max_file_size_bytes = max_file.Value();

  auto chunk =
      PickUint(dotenv, "DEFAULT_CHUNK_SIZE", 8ULL * 1024 * 1024, kMinChunkSize, kMaxChunkSize);
  if (!chunk) {
    return chunk.GetError();
  }
  config.default_chunk_size_bytes = chunk.Value();

  if (config.default_chunk_size_bytes > config.max_file_size_bytes) {
    return ConfigError("DEFAULT_CHUNK_SIZE 不能大于 MAX_FILE_SIZE");
  }

  auto uploads = PickUint(dotenv, "MAX_CONCURRENT_UPLOADS", 16, 1, 1024);
  if (!uploads) {
    return uploads.GetError();
  }
  config.max_concurrent_uploads = static_cast<std::uint32_t>(uploads.Value());

  auto uploads_per_user = PickUint(dotenv, "MAX_CONCURRENT_UPLOADS_PER_USER", 4, 1, 1024);
  if (!uploads_per_user) {
    return uploads_per_user.GetError();
  }
  config.max_concurrent_uploads_per_user = static_cast<std::uint32_t>(uploads_per_user.Value());
  if (config.max_concurrent_uploads_per_user > config.max_concurrent_uploads) {
    return ConfigError("MAX_CONCURRENT_UPLOADS_PER_USER 不能大于 MAX_CONCURRENT_UPLOADS");
  }

  auto quota = PickUint(dotenv, "MAX_USER_STORAGE_BYTES", 20ULL * 1024 * 1024 * 1024, 1,
                        kMaxAllowedFileSize * 16ULL);
  if (!quota) {
    return quota.GetError();
  }
  config.max_user_storage_bytes = quota.Value();

  auto free_disk = PickUint(dotenv, "MIN_FREE_DISK_BYTES", 1ULL * 1024 * 1024 * 1024, 0,
                            kMaxAllowedFileSize * 16ULL);
  if (!free_disk) {
    return free_disk.GetError();
  }
  config.min_free_disk_bytes = free_disk.Value();

  auto login_rate = PickUint(dotenv, "LOGIN_RATE_LIMIT_PER_MINUTE", 10, 1, 100000);
  if (!login_rate) {
    return login_rate.GetError();
  }
  config.login_rate_limit_per_minute = static_cast<std::uint32_t>(login_rate.Value());

  auto login_ip_rate = PickUint(dotenv, "LOGIN_IP_RATE_LIMIT_PER_MINUTE", 60, 1, 100000);
  if (!login_ip_rate) {
    return login_ip_rate.GetError();
  }
  config.login_ip_rate_limit_per_minute = static_cast<std::uint32_t>(login_ip_rate.Value());

  auto upload_rate = PickUint(dotenv, "UPLOAD_RATE_LIMIT_PER_MINUTE", 120, 1, 100000);
  if (!upload_rate) {
    return upload_rate.GetError();
  }
  config.upload_rate_limit_per_minute = static_cast<std::uint32_t>(upload_rate.Value());

  auto ttl = PickUint(dotenv, "UPLOAD_SESSION_TTL", 24ULL * 60 * 60, 60, 30ULL * 24 * 60 * 60);
  if (!ttl) {
    return ttl.GetError();
  }
  config.upload_session_ttl_seconds = static_cast<std::uint32_t>(ttl.Value());

  auto jwt_expire = PickUint(dotenv, "JWT_EXPIRE_SECONDS", 3600, 60, 30ULL * 24 * 60 * 60);
  if (!jwt_expire) {
    return jwt_expire.GetError();
  }
  config.jwt_expire_seconds = static_cast<std::uint32_t>(jwt_expire.Value());

  auto rate = PickUint(dotenv, "DOWNLOAD_RATE_LIMIT", 0, 0, 1ULL << 40);
  if (!rate) {
    return rate.GetError();
  }
  config.download_rate_limit_bytes_per_second = rate.Value();

  config.cors_allowed_origins =
      SplitList(PickString(dotenv, "CORS_ALLOWED_ORIGINS", "http://localhost:5173"));
  if (config.cors_allowed_origins.empty()) {
    return ConfigError("CORS_ALLOWED_ORIGINS 不能为空；不允许使用 * 通配");
  }
  if (std::ranges::find(config.cors_allowed_origins, "*") != config.cors_allowed_origins.end()) {
    return ConfigError("CORS_ALLOWED_ORIGINS 不允许使用 * 通配");
  }

  config.jwt_secret = Trim(PickString(dotenv, "JWT_SECRET", ""));

  // ---- 生产环境额外检查（docs/deployment.md）----
  if (config.IsProduction()) {
    if (config.jwt_secret.size() < 32) {
      return ConfigError("生产环境必须设置 JWT_SECRET，且长度不少于 32 个字符");
    }
    if (auto password = ExtractPassword(config.database.url);
        password.has_value() && *password == std::string(kDevDatabasePassword)) {
      return ConfigError("生产环境不允许使用开发默认数据库口令，请改用独立凭据");
    }
  }

  // ---- 收集需要脱敏的值：日志与错误信息输出前一律替换 ----
  if (auto password = ExtractPassword(config.database.url); password.has_value()) {
    result.secrets.push_back(*password);
  }
  if (auto password = ExtractPassword(config.redis.url); password.has_value()) {
    result.secrets.push_back(*password);
  }
  if (!config.jwt_secret.empty()) {
    result.secrets.push_back(config.jwt_secret);
  }

  result.config = std::move(config);
  return result;
}

}  // namespace lan_file_hub
