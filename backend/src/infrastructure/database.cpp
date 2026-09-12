#include "lan_file_hub/infrastructure/database.h"

#include <utility>

#include <pqxx/pqxx>

namespace lan_file_hub {
namespace {

// postgresql:// 或 keyword=value 两种形式都补上 connect_timeout
std::string BuildConnInfo(const DatabaseConfig& config) {
  const auto seconds = std::max<std::uint32_t>(1, config.connect_timeout_ms / 1000);
  const std::string timeout = "connect_timeout=" + std::to_string(seconds);
  std::string info = config.url;
  if (info.find("://") != std::string::npos) {
    info += (info.find('?') == std::string::npos) ? "?" : "&";
    info += timeout;
  } else {
    info += " ";
    info += timeout;
  }
  return info;
}

}  // namespace

ConnectionLease::ConnectionLease(ConnectionPool* pool, std::size_t index,
                                 pqxx::connection* connection)
    : pool_(pool), index_(index), connection_(connection) {}

ConnectionLease::~ConnectionLease() {
  if (pool_ != nullptr) {
    pool_->Release(index_);
  }
}

ConnectionPool::ConnectionPool(std::string conn_info, std::size_t size)
    : conn_info_(std::move(conn_info)) {
  connections_.resize(size);
  in_use_.assign(size, false);
}

ConnectionPool::~ConnectionPool() = default;

Result<std::shared_ptr<ConnectionPool>> ConnectionPool::Create(const DatabaseConfig& config,
                                                               std::size_t size) {
  if (config.url.empty()) {
    return Error{"STORAGE_UNAVAILABLE", "DATABASE_URL 未配置"};
  }
  if (size == 0) {
    size = 1;
  }

  auto pool = std::shared_ptr<ConnectionPool>(new ConnectionPool(BuildConnInfo(config), size));
  for (std::size_t i = 0; i < size; ++i) {
    try {
      pool->connections_[i] = std::make_unique<pqxx::connection>(pool->conn_info_);
      if (!pool->connections_[i]->is_open()) {
        return Error{"STORAGE_UNAVAILABLE", "数据库连接未打开"};
      }
    } catch (const std::exception& ex) {
      // 注意：异常文本可能含连接串，调用方负责脱敏后再输出
      return Error{"STORAGE_UNAVAILABLE", "无法建立数据库连接", ex.what()};
    }
  }
  return pool;
}

bool ConnectionPool::EnsureUsable(std::size_t index) {
  try {
    if (connections_[index] != nullptr && connections_[index]->is_open()) {
      return true;
    }
    connections_[index] = std::make_unique<pqxx::connection>(conn_info_);
    return connections_[index]->is_open();
  } catch (const std::exception&) {
    return false;
  }
}

std::unique_ptr<ConnectionLease> ConnectionPool::Acquire(std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(mutex_);
  const bool got = available_.wait_for(lock, timeout, [this] {
    for (const bool used : in_use_) {
      if (!used)
        return true;
    }
    return false;
  });
  if (!got) {
    return nullptr;
  }

  for (std::size_t i = 0; i < in_use_.size(); ++i) {
    if (in_use_[i])
      continue;
    in_use_[i] = true;
    lock.unlock();

    if (!EnsureUsable(i)) {
      Release(i);
      return nullptr;
    }
    return std::make_unique<ConnectionLease>(this, i, connections_[i].get());
  }
  return nullptr;
}

void ConnectionPool::Release(std::size_t index) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    in_use_[index] = false;
  }
  available_.notify_one();
}

}  // namespace lan_file_hub
