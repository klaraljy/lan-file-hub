// 字节级令牌桶：按 key（用户或分享链接）限制传输速率（阶段十二第 4 条：下载限速）。
// 与 RateLimiter 的区别：那个数"请求次数"，这个数"字节数"，并且允许一次只放行一部分。
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace lan_file_hub {

class ByteRateLimiter {
 public:
  struct Grant {
    std::size_t bytes{0};                      // 本次允许立刻发送的字节数；0 表示需要等待
    std::chrono::milliseconds retry_after{0};  // bytes==0 时建议的等待时长（至少 1ms）
  };

  // bytes_per_second == 0 表示不限速（enabled() 为 false，Acquire 直接全量放行）
  explicit ByteRateLimiter(std::uint64_t bytes_per_second, std::size_t max_keys = 4096);

  [[nodiscard]] bool enabled() const noexcept { return bytes_per_second_ > 0; }
  [[nodiscard]] std::uint64_t bytes_per_second() const noexcept { return bytes_per_second_; }

  // 取走至多 want 字节的令牌。返回值里的 bytes 可能小于 want（桶里只剩这么多）。
  [[nodiscard]] Grant Acquire(std::string_view key, std::size_t want);

  // 清掉某个 key 的桶（例如下载结束后释放）
  void Reset(std::string_view key);

  [[nodiscard]] std::size_t Size() const;

 private:
  using Clock = std::chrono::steady_clock;

  struct Bucket {
    double tokens{0.0};
    Clock::time_point last_seen{};
    bool initialised{false};
  };

  void Refill(Bucket& bucket, Clock::time_point now) const;
  void EvictIfNeeded();

  std::uint64_t bytes_per_second_{0};
  double capacity_{0.0};  // 突发额度 = 1 秒的量：小文件一次性发完，不为难交互式下载
  std::size_t max_keys_{4096};
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Bucket> buckets_;
};

}  // namespace lan_file_hub
