#include "lan_file_hub/common/rate_limiter.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace lan_file_hub {
namespace {

using Clock = std::chrono::steady_clock;

}  // namespace

RateLimiter::RateLimiter(int max_events, std::chrono::seconds window, std::size_t max_keys)
    : max_events_(std::max(max_events, 1)),
      window_(std::max(window, std::chrono::seconds(1))),
      max_keys_(std::max<std::size_t>(max_keys, 16)) {}

void RateLimiter::DropExpired(Bucket& bucket, Clock::time_point now) const {
  while (!bucket.events.empty() && now - bucket.events.front() >= window_) {
    bucket.events.pop_front();
  }
}

void RateLimiter::EvictIfNeeded(std::unique_lock<std::mutex>& /*lock*/) {
  if (buckets_.size() < max_keys_) {
    return;
  }
  // 满了才清理：先删已经空掉的桶，再删最久没被访问的若干个。
  // 不做"每来一个请求就全表扫描"，避免限流器自己变成 CPU 热点。
  const auto now = Clock::now();
  for (auto it = buckets_.begin(); it != buckets_.end();) {
    DropExpired(it->second, now);
    if (it->second.events.empty()) {
      it = buckets_.erase(it);
    } else {
      ++it;
    }
  }
  if (buckets_.size() < max_keys_) {
    return;
  }
  // 还有大量活跃 key：淘汰最久未访问的 1/8，保证摊销成本可控
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

RateLimitDecision RateLimiter::Acquire(std::string_view key) {
  const auto now = Clock::now();
  std::unique_lock<std::mutex> lock(mutex_);
  EvictIfNeeded(lock);

  Bucket& bucket = buckets_[std::string(key)];
  bucket.last_seen = now;
  DropExpired(bucket, now);

  RateLimitDecision decision;
  if (static_cast<int>(bucket.events.size()) >= max_events_) {
    // 被拒绝时不记账：否则持续轰炸会把窗口一直"顶住"，变成永久封禁
    const auto oldest = bucket.events.front();
    const auto unlock_at = oldest + window_;
    decision.allowed = false;
    decision.remaining = 0;
    decision.retry_after_seconds =
        static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(unlock_at - now).count());
    if (decision.retry_after_seconds < 1) {
      decision.retry_after_seconds = 1;
    }
    return decision;
  }

  bucket.events.push_back(now);
  decision.allowed = true;
  decision.remaining = max_events_ - static_cast<int>(bucket.events.size());
  return decision;
}

RateLimitDecision RateLimiter::Peek(std::string_view key) const {
  const auto now = Clock::now();
  std::lock_guard<std::mutex> lock(mutex_);

  RateLimitDecision decision;
  const auto found = buckets_.find(std::string(key));
  if (found == buckets_.end()) {
    decision.remaining = max_events_;
    return decision;
  }

  Bucket bucket = found->second;  // 拷贝出来清理，保证 Peek 不修改状态
  DropExpired(bucket, now);
  decision.allowed = static_cast<int>(bucket.events.size()) < max_events_;
  decision.remaining = std::max(max_events_ - static_cast<int>(bucket.events.size()), 0);
  if (!decision.allowed) {
    const auto unlock_at = bucket.events.front() + window_;
    decision.retry_after_seconds =
        std::max(1, static_cast<int>(
                        std::chrono::duration_cast<std::chrono::seconds>(unlock_at - now).count()));
  }
  return decision;
}

void RateLimiter::Reset(std::string_view key) {
  std::lock_guard<std::mutex> lock(mutex_);
  buckets_.erase(std::string(key));
}

std::size_t RateLimiter::Size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return buckets_.size();
}

std::string ClientIpFrom(std::string_view forwarded_for, std::string_view real_ip,
                         std::string_view peer_address) {
  const auto first_of = [](std::string_view value) -> std::string {
    const auto comma = value.find(',');
    std::string_view head = comma == std::string_view::npos ? value : value.substr(0, comma);
    while (!head.empty() && (head.front() == ' ' || head.front() == '\t')) {
      head.remove_prefix(1);
    }
    while (!head.empty() && (head.back() == ' ' || head.back() == '\t')) {
      head.remove_suffix(1);
    }
    return std::string(head);
  };

  if (!forwarded_for.empty()) {
    const std::string candidate = first_of(forwarded_for);
    if (!candidate.empty()) {
      return candidate;
    }
  }
  if (!real_ip.empty()) {
    const std::string candidate = first_of(real_ip);
    if (!candidate.empty()) {
      return candidate;
    }
  }
  return std::string(peer_address);
}

}  // namespace lan_file_hub
