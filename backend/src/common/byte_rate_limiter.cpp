#include "lan_file_hub/common/byte_rate_limiter.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace lan_file_hub {
namespace {

using Clock = std::chrono::steady_clock;

}  // namespace

ByteRateLimiter::ByteRateLimiter(std::uint64_t bytes_per_second, std::size_t max_keys)
    : bytes_per_second_(bytes_per_second),
      capacity_(static_cast<double>(bytes_per_second)),
      max_keys_(std::max<std::size_t>(max_keys, 16)) {}

void ByteRateLimiter::Refill(Bucket& bucket, Clock::time_point now) const {
  const double elapsed = std::chrono::duration<double>(now - bucket.last_seen).count();
  bucket.last_seen = now;
  if (elapsed <= 0.0) {
    return;
  }
  bucket.tokens =
      std::min(capacity_, bucket.tokens + elapsed * static_cast<double>(bytes_per_second_));
}

void ByteRateLimiter::EvictIfNeeded() {
  if (buckets_.size() < max_keys_) {
    return;
  }
  // 与 RateLimiter 同样的策略：先删已经闲下来的桶，仍不够就淘汰最久未访问的 1/8，
  // 避免攻击者用海量 key 把限速器自己变成内存热点。
  const auto now = Clock::now();
  for (auto it = buckets_.begin(); it != buckets_.end();) {
    const double idle = std::chrono::duration<double>(now - it->second.last_seen).count();
    if (idle > 60.0) {
      it = buckets_.erase(it);
    } else {
      ++it;
    }
  }
  if (buckets_.size() < max_keys_) {
    return;
  }
  std::vector<std::pair<Clock::time_point, std::string>> ordered;
  ordered.reserve(buckets_.size());
  for (const auto& [key, bucket] : buckets_) {
    ordered.emplace_back(bucket.last_seen, key);
  }
  const std::size_t to_remove = std::max<std::size_t>(ordered.size() / 8, 1);
  std::partial_sort(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(to_remove),
                    ordered.end());
  for (std::size_t i = 0; i < to_remove; ++i) {
    buckets_.erase(ordered[i].second);
  }
}

ByteRateLimiter::Grant ByteRateLimiter::Acquire(std::string_view key, std::size_t want) {
  if (!enabled() || want == 0) {
    return Grant{want, std::chrono::milliseconds{0}};
  }

  const auto now = Clock::now();
  std::lock_guard<std::mutex> lock(mutex_);
  EvictIfNeeded();

  Bucket& bucket = buckets_[std::string(key)];
  if (!bucket.initialised) {
    // 新桶先给满 1 秒的突发额度：小文件一次发完，不影响交互体验
    bucket.tokens = capacity_;
    bucket.initialised = true;
    bucket.last_seen = now;
  }
  Refill(bucket, now);

  Grant grant;
  if (bucket.tokens >= 1.0) {
    const auto available = static_cast<std::size_t>(bucket.tokens);
    grant.bytes = std::min(want, available);
    bucket.tokens -= static_cast<double>(grant.bytes);
    return grant;
  }

  const double missing = 1.0 - bucket.tokens;
  const double wait_ms = missing / static_cast<double>(bytes_per_second_) * 1000.0;
  grant.bytes = 0;
  grant.retry_after = std::chrono::milliseconds(
      std::max<std::int64_t>(static_cast<std::int64_t>(std::ceil(wait_ms)), 1));
  return grant;
}

void ByteRateLimiter::Reset(std::string_view key) {
  std::lock_guard<std::mutex> lock(mutex_);
  buckets_.erase(std::string(key));
}

std::size_t ByteRateLimiter::Size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return buckets_.size();
}

}  // namespace lan_file_hub
