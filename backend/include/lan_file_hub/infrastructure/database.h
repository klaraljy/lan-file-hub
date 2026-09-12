// 数据库连接池。
//
// 设计取舍：
//   - 固定容量，启动时建立；借还都有 RAII（ConnectionLease），不会漏归还。
//   - 借出时检查连接是否仍然可用，断了就重连（PostgreSQL 重启后能自愈）。
//   - 所有 SQL 一律参数化，见 docs/security.md；禁止把用户输入拼进语句。
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "lan_file_hub/common/config.h"
#include "lan_file_hub/common/result.h"

namespace pqxx {
class connection;
}  // namespace pqxx

namespace lan_file_hub {

class ConnectionPool;

// 连接租约。离开作用域自动归还；不可拷贝。
class ConnectionLease {
 public:
  ConnectionLease(ConnectionPool* pool, std::size_t index, pqxx::connection* connection);
  ~ConnectionLease();

  ConnectionLease(const ConnectionLease&) = delete;
  ConnectionLease& operator=(const ConnectionLease&) = delete;
  ConnectionLease(ConnectionLease&&) = delete;
  ConnectionLease& operator=(ConnectionLease&&) = delete;

  [[nodiscard]] pqxx::connection& connection() const { return *connection_; }

 private:
  ConnectionPool* pool_;
  std::size_t index_;
  pqxx::connection* connection_;
};

class ConnectionPool : public std::enable_shared_from_this<ConnectionPool> {
 public:
  // 建立连接池；任何一条连接建不起来就整体失败（宁可启动失败也不带病运行）。
  [[nodiscard]] static Result<std::shared_ptr<ConnectionPool>> Create(const DatabaseConfig& config,
                                                                      std::size_t size);

  ~ConnectionPool();
  ConnectionPool(const ConnectionPool&) = delete;
  ConnectionPool& operator=(const ConnectionPool&) = delete;

  // 借出一个连接；超过 timeout 仍无可用连接则返回 nullptr。
  [[nodiscard]] std::unique_ptr<ConnectionLease> Acquire(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));

  [[nodiscard]] std::size_t size() const { return connections_.size(); }

 private:
  friend class ConnectionLease;

  ConnectionPool(std::string conn_info, std::size_t size);

  void Release(std::size_t index);
  [[nodiscard]] bool EnsureUsable(std::size_t index);

  std::string conn_info_;
  std::vector<std::unique_ptr<pqxx::connection>> connections_;
  std::vector<bool> in_use_;
  std::mutex mutex_;
  std::condition_variable available_;
};

}  // namespace lan_file_hub
