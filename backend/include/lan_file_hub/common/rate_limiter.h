// 进程内滑动窗口限流器（阶段十二）。
//
// 为什么不用 Redis：限流的目的是挡住暴力破解与滥用，**限流器本身不能成为新的单点故障**。
// 单节点部署下进程内计数已经足够；Redis 不可用时"限流失效"比"登录不可用"更可接受，
// 但反过来把登录挂在 Redis 上就多了一个可用性风险。需要多节点共享计数时再换实现
// （接口刻意做成可替换的）。
//
// 内存安全：key 来自用户输入（IP、用户名），如果无上限地建桶，
// 攻击者用随机用户名就能把内存打满 —— 所以对桶数量设硬上限，
// 超限时淘汰最久未使用的空桶/旧桶，并且**在淘汰时不影响正在被限流的 key**。
#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace lan_file_hub {

struct RateLimitDecision {
  bool allowed{true};
  int remaining{0};
  // 被拒绝时建议客户端等待的秒数（对应 Retry-After 响应头）
  int retry_after_seconds{0};
};

class RateLimiter {
 public:
  // max_events：窗口内允许的次数；window：窗口长度
  RateLimiter(int max_events, std::chrono::seconds window, std::size_t max_keys = 10000);

  // 记一次事件并返回是否放行
  [[nodiscard]] RateLimitDecision Acquire(std::string_view key);

  // 只查询不记账（用于展示"还剩几次"，不改变状态）
  [[nodiscard]] RateLimitDecision Peek(std::string_view key) const;

  // 成功后清空某个 key 的计数（登录成功就不该继续累计失败次数）
  void Reset(std::string_view key);

  [[nodiscard]] std::size_t Size() const;

  [[nodiscard]] int max_events() const { return max_events_; }

 private:
  struct Bucket {
    std::deque<std::chrono::steady_clock::time_point> events;
    std::chrono::steady_clock::time_point last_seen{};
  };

  void EvictIfNeeded(std::unique_lock<std::mutex>& lock);
  void DropExpired(Bucket& bucket, std::chrono::steady_clock::time_point now) const;

  int max_events_;
  std::chrono::seconds window_;
  std::size_t max_keys_;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, Bucket> buckets_;
};

// 从 X-Forwarded-For / X-Real-IP 取真实客户端 IP（部署在反向代理后面时用），
// 两者都没有则回退到直连地址。取**最左边**的一段：那是最初的客户端。
// 注意：只有在可信代理后才可以信任这些头；本项目默认直连，配置了反代时应由反代覆盖。
[[nodiscard]] std::string ClientIpFrom(std::string_view forwarded_for, std::string_view real_ip,
                                       std::string_view peer_address);

}  // namespace lan_file_hub
