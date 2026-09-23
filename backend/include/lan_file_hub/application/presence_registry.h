// 在线状态注册表（五阶段改造 ⑤：广场 / 在线用户，用户 2026-09-21 指示）。
//
// 设计选择：**进程内存**，不用 Redis ——
//   - 在线状态本来就易失（心跳 + 90 秒超时），丢了不影响任何业务数据；
//   - 不必为每次心跳建一条 Redis 连接，也不必新写一套 Redis 客户端（现在只有健康检查内联用过 hiredis）；
//   - 便携包里少一个"必须活着"的依赖。
// ⚠️ 已知限制：多实例部署时各实例各自记账；本项目是单机单进程（AGENTS §15.1），暂不需要分布式在线状态。
//
// 隐私：每条记录带 `visible`（来自用户的设置，默认可见）。别人查在线列表时**看不到隐身用户**；
// 用户自己始终能看到自己的所有设备。
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lan_file_hub {

// 多久没心跳就算离线（客户端每 30 秒心跳一次，留 3 倍余量）
inline constexpr std::int64_t kPresenceTtlSeconds = 90;

struct PresenceEntry {
  std::string user_id;
  std::string username;
  std::string device_id;    // 客户端生成的稳定标识（存在浏览器本地）
  std::string device_name;  // 展示用："Chrome · Windows"
  std::string platform;     // web / android / windows
  bool visible{true};
  std::int64_t last_seen_unix{0};
};

[[nodiscard]] std::int64_t NowUnixSeconds();

class PresenceRegistry {
 public:
  // 心跳：插入或刷新一条设备记录
  void Touch(PresenceEntry entry, std::int64_t now_unix);

  // 用户改「隐身/可见」后立刻生效（其它用户要等自己下次心跳才刷新，最多 30 秒）
  void SetVisibility(std::string_view user_id, bool visible);

  // 主动下线（例如关页面时调用；不调也会被 TTL 清掉）
  void Remove(std::string_view user_id, std::string_view device_id);

  // 别人看到的在线用户：排除自己、排除隐身用户；同一个用户多设备只保留最新一条
  [[nodiscard]] std::vector<PresenceEntry> OnlineOthers(std::string_view exclude_user_id,
                                                        std::int64_t now_unix) const;

  // 自己看到的所有设备（含隐身状态下的自己）
  [[nodiscard]] std::vector<PresenceEntry> DevicesOf(std::string_view user_id,
                                                     std::int64_t now_unix) const;

  // 清理超时记录，返回清掉的条数（由请求路径顺手调用，不额外起线程）
  [[nodiscard]] std::size_t Sweep(std::int64_t now_unix);

  [[nodiscard]] std::size_t Size() const;

 private:
  [[nodiscard]] static bool Fresh(const PresenceEntry& entry, std::int64_t now_unix) {
    return now_unix - entry.last_seen_unix <= kPresenceTtlSeconds;
  }
  [[nodiscard]] static std::string Key(std::string_view user_id, std::string_view device_id);

  mutable std::mutex mutex_;
  std::unordered_map<std::string, PresenceEntry> entries_;
};

}  // namespace lan_file_hub
