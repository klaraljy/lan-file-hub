// 数据库迁移工具。
//
//   lan_file_hub_migrate [--migrations <dir>] [--env-file <path>]
//                       [--create-admin <username>] [--admin-email <email>]
//                       [--admin-password-env <VAR>]
//
// 行为：
//   - 读取 migrations/ 下按文件名排序的 *.sql
//   - 在 schema_migrations 表里记录已应用的版本
//   - 每个文件在一个事务里执行，失败整体回滚并返回非零退出码
//
// 为什么不放在服务启动时自动跑：生产环境不应该由应用进程改 schema
// （见 docs/deployment.md「添加数据库迁移执行方式」）。
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <pqxx/pqxx>

#include "lan_file_hub/common/config.h"
#include "lan_file_hub/common/logging.h"
#include "lan_file_hub/infrastructure/password_hasher.h"

namespace {

namespace fs = std::filesystem;

struct Options {
  fs::path migrations_dir{"migrations"};
  fs::path env_file{".env"};
  // 阶段十五第 10 条：初始化管理员。用户名留空表示不做这件事。
  std::string admin_username;
  std::string admin_email;
  // 口令只从环境变量读，绝不接受命令行参数（会进 shell 历史与进程列表）
  std::string admin_password_env{"INITIAL_ADMIN_PASSWORD"};
};

Options ParseArgs(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--migrations" && i + 1 < argc) {
      options.migrations_dir = argv[++i];
    } else if (arg == "--env-file" && i + 1 < argc) {
      options.env_file = argv[++i];
    } else if (arg == "--create-admin" && i + 1 < argc) {
      options.admin_username = argv[++i];
    } else if (arg == "--admin-email" && i + 1 < argc) {
      options.admin_email = argv[++i];
    } else if (arg == "--admin-password-env" && i + 1 < argc) {
      options.admin_password_env = argv[++i];
    }
  }
  return options;
}

std::string ReadFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

}  // namespace

int main(int argc, char** argv) {
  using namespace lan_file_hub;

  const Options options = ParseArgs(argc, argv);

  auto loaded = LoadConfig(options.env_file);
  if (!loaded.Ok()) {
    std::cerr << "配置错误: " << loaded.GetError().message;
    if (!loaded.GetError().detail.empty()) {
      std::cerr << "（" << loaded.GetError().detail << "）";
    }
    std::cerr << std::endl;
    return 1;
  }
  const AppConfig& config = loaded.Value().config;

  auto logging = InitializeLogging(LoggingOptions{"info", "", {}});
  if (!logging.Ok()) {
    std::cerr << "日志初始化失败: " << logging.GetError().message << std::endl;
    return 1;
  }

  std::error_code ec;
  if (!fs::is_directory(options.migrations_dir, ec)) {
    std::cerr << "迁移目录不存在: " << options.migrations_dir.string() << std::endl;
    return 1;
  }

  std::vector<fs::path> files;
  for (const auto& entry : fs::directory_iterator(options.migrations_dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".sql") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  if (files.empty()) {
    std::cout << "没有找到任何 .sql 迁移文件" << std::endl;
    return 0;
  }

  try {
    pqxx::connection connection(config.database.url);

    {
      pqxx::work tx(connection);
      tx.exec(
          "CREATE TABLE IF NOT EXISTS schema_migrations ("
          "  version TEXT PRIMARY KEY,"
          "  applied_at TIMESTAMPTZ NOT NULL DEFAULT now())");
      tx.commit();
    }

    int applied = 0;
    int skipped = 0;
    for (const auto& file : files) {
      const std::string version = file.stem().string();

      {
        pqxx::work tx(connection);
        pqxx::params params;
        params.append(version);
        const pqxx::result rows =
            tx.exec("SELECT 1 FROM schema_migrations WHERE version = $1", params);
        tx.commit();
        if (!rows.empty()) {
          std::cout << "跳过（已应用）: " << version << std::endl;
          ++skipped;
          continue;
        }
      }

      const std::string sql = ReadFile(file);
      try {
        pqxx::work tx(connection);
        tx.exec(sql);
        pqxx::params params;
        params.append(version);
        tx.exec("INSERT INTO schema_migrations (version) VALUES ($1)", params);
        tx.commit();
        std::cout << "已应用: " << version << std::endl;
        ++applied;
      } catch (const std::exception& ex) {
        // 迁移内容来自仓库，不含凭据，可以直接打印
        std::cerr << "迁移失败: " << version << " -> " << ex.what() << std::endl;
        return 1;
      }
    }

    std::cout << "完成：新应用 " << applied << " 个，跳过 " << skipped << " 个" << std::endl;

    // ---- 初始化管理员（阶段十五第 10 条）----
    if (!options.admin_username.empty()) {
      // 跨平台读取（MSVC 用 _dupenv_s 规避 C4996，其它平台用 getenv）
      const std::string password = GetEnvVar(options.admin_password_env.c_str()).value_or("");
      if (password.empty()) {
        std::cerr << "初始化管理员失败：环境变量 " << options.admin_password_env
                  << " 未设置或为空。仓库里不放默认口令，请显式提供（至少 " << kMinPasswordBytes
                  << " 位）" << std::endl;
        return 1;
      }
      if (password.size() < kMinPasswordBytes) {
        std::cerr << "初始化管理员失败：口令至少 " << kMinPasswordBytes << " 位" << std::endl;
        return 1;
      }

      const PasswordHasher hasher;
      auto encoded = hasher.Hash(password);
      if (!encoded.Ok()) {
        std::cerr << "初始化管理员失败：口令哈希计算失败" << std::endl;
        return 1;
      }

      pqxx::work tx(connection);
      pqxx::params find_params;
      find_params.append(options.admin_username);
      const pqxx::result existing =
          tx.exec("SELECT id::text AS id, role FROM users WHERE username = $1", find_params);
      if (!existing.empty()) {
        const std::string user_id = existing[0]["id"].as<std::string>();
        const std::string role = existing[0]["role"].as<std::string>();
        if (role == "ADMIN") {
          std::cout << "初始化管理员：用户 " << options.admin_username << " 已是管理员，未改动"
                    << std::endl;
        } else {
          pqxx::params promote;
          promote.append(user_id);
          tx.exec("UPDATE users SET role = 'ADMIN', updated_at = now() WHERE id = $1::uuid",
                  promote);
          std::cout << "初始化管理员：已把已有用户 " << options.admin_username
                    << " 提升为管理员（口令未改动）" << std::endl;
        }
      } else {
        if (options.admin_email.empty()) {
          std::cerr << "初始化管理员失败：新用户必须用 --admin-email 指定邮箱" << std::endl;
          return 1;
        }
        pqxx::params insert;
        insert.append(options.admin_username);
        insert.append(options.admin_email);
        insert.append(encoded.Value());
        insert.append(config.max_user_storage_bytes);
        tx.exec(
            "INSERT INTO users (id, username, email, password_hash, role, status, "
            "storage_quota_bytes, used_storage_bytes) "
            "VALUES (gen_random_uuid(), $1, $2, $3, 'ADMIN', 'ACTIVE', $4, 0)",
            insert);
        std::cout << "初始化管理员：已创建管理员用户 " << options.admin_username << std::endl;
      }
      tx.commit();
      std::cout << "提示：这是引导口令，登录后请立即修改；仓库中不保存任何默认口令。" << std::endl;
    }
  } catch (const std::exception& ex) {
    std::cerr << "数据库错误: " << ex.what() << std::endl;
    return 1;
  }

  ShutdownLogging();
  return 0;
}
