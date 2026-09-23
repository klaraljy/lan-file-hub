#include "lan_file_hub/application/presence_registry.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace lan_file_hub {

std::int64_t NowUnixSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string PresenceRegistry::Key(std::string_view user_id, std::string_view device_id) {
  std::string key;
  key.reserve(user_id.size() + device_id.size() + 1);
  key.append(user_id);
  key.push_back('|');
  key.append(device_id);
  return key;
}

void PresenceRegistry::Touch(PresenceEntry entry, std::int64_t now_unix) {
  entry.last_seen_unix = now_unix;
  std::lock_guard<std::mutex> lock(mutex_);
  entries_[Key(entry.user_id, entry.device_id)] = std::move(entry);
}

void PresenceRegistry::SetVisibility(std::string_view user_id, bool visible) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& [key, entry] : entries_) {
    if (entry.user_id == user_id) {
      entry.visible = visible;
    }
  }
}

void PresenceRegistry::Remove(std::string_view user_id, std::string_view device_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.erase(Key(user_id, device_id));
}

std::vector<PresenceEntry> PresenceRegistry::OnlineOthers(std::string_view exclude_user_id,
                                                          std::int64_t now_unix) const {
  std::lock_guard<std::mutex> lock(mutex_);
  // 同一个用户可能多设备在线：只留最新心跳的那台，避免列表里同一个人出现好几次
  std::unordered_map<std::string, PresenceEntry> latest_per_user;
  for (const auto& [key, entry] : entries_) {
    if (entry.user_id == exclude_user_id || !entry.visible || !Fresh(entry, now_unix)) {
      continue;
    }
    auto found = latest_per_user.find(entry.user_id);
    if (found == latest_per_user.end() || entry.last_seen_unix > found->second.last_seen_unix) {
      latest_per_user[entry.user_id] = entry;
    }
  }

  std::vector<PresenceEntry> result;
  result.reserve(latest_per_user.size());
  for (auto& [user_id, entry] : latest_per_user) {
    result.push_back(std::move(entry));
  }
  // 最近活跃的排在前面，前端直接按顺序渲染
  std::sort(result.begin(), result.end(),
            [](const PresenceEntry& left, const PresenceEntry& right) {
              if (left.last_seen_unix != right.last_seen_unix) {
                return left.last_seen_unix > right.last_seen_unix;
              }
              return left.username < right.username;
            });
  return result;
}

std::vector<PresenceEntry> PresenceRegistry::DevicesOf(std::string_view user_id,
                                                       std::int64_t now_unix) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<PresenceEntry> result;
  for (const auto& [key, entry] : entries_) {
    if (entry.user_id == user_id && Fresh(entry, now_unix)) {
      result.push_back(entry);
    }
  }
  std::sort(result.begin(), result.end(),
            [](const PresenceEntry& left, const PresenceEntry& right) {
              return left.last_seen_unix > right.last_seen_unix;
            });
  return result;
}

std::size_t PresenceRegistry::Sweep(std::int64_t now_unix) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::size_t removed = 0;
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (!Fresh(it->second, now_unix)) {
      it = entries_.erase(it);
      ++removed;
    } else {
      ++it;
    }
  }
  return removed;
}

std::size_t PresenceRegistry::Size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_.size();
}

}  // namespace lan_file_hub
