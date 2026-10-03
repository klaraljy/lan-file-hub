#include "lan_file_hub/application/app_release.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "lan_file_hub/common/sha256.h"

namespace lan_file_hub {
namespace {

// file_time_type → time_t。
// 用 C++20 的 clock_cast（MSVC STL 的 file_clock 没有公开的 to_sys，
// 而 libstdc++ 有 —— clock_cast 两边都能用，是唯一可移植的写法）。
std::time_t ToTimeT(const std::filesystem::file_time_type& value) {
  const auto sys_time = std::chrono::clock_cast<std::chrono::system_clock>(value);
  return std::chrono::system_clock::to_time_t(
      std::chrono::time_point_cast<std::chrono::system_clock::duration>(sys_time));
}

// UTC ISO8601，与 docs/api.md 的「统一时间格式」一致（数据库那边用 to_char，这里是文件时间）
std::string ToIso8601Utc(std::time_t seconds) {
  std::tm parts{};
#if defined(_MSC_VER)
  if (gmtime_s(&parts, &seconds) != 0) {
    return {};
  }
#else
  if (gmtime_r(&seconds, &parts) == nullptr) {
    return {};
  }
#endif
  char buffer[32]{};
  const int written =
      std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ", parts.tm_year + 1900,
                    parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec);
  if (written <= 0) {
    return {};
  }
  return std::string(buffer);
}

// 侧车里的字符串字段：类型不对或没有就返回空串（构建脚本换版本时不能把接口带崩）
std::string ManifestString(const nlohmann::json& manifest, const char* key) {
  const auto it = manifest.find(key);
  if (it == manifest.end() || !it->is_string()) {
    return {};
  }
  return it->get<std::string>();
}

std::uint32_t ManifestUint(const nlohmann::json& manifest, const char* key) {
  const auto it = manifest.find(key);
  if (it == manifest.end() || !it->is_number_unsigned()) {
    return 0;
  }
  return it->get<std::uint32_t>();
}

// 侧车里的字节数：用来判断"这份侧车说的还是不是磁盘上这个包"（见 Describe 里的说明）
std::uint64_t ManifestBytes(const nlohmann::json& manifest, const char* key) {
  const auto it = manifest.find(key);
  if (it == manifest.end() || !it->is_number_unsigned()) {
    return 0;
  }
  return it->get<std::uint64_t>();
}

bool SameStamp(const std::filesystem::file_time_type& a, const std::filesystem::file_time_type& b) {
  return a == b;
}

}  // namespace

AppRelease::AppRelease(std::filesystem::path apk_path)
    : apk_path_(std::move(apk_path)), manifest_path_(ManifestPathFor(apk_path_)) {}

std::filesystem::path AppRelease::ManifestPathFor(const std::filesystem::path& apk_path) {
  if (apk_path.empty()) {
    return {};
  }
  // 侧车文件名 = APK 文件名 + ".json"（构建脚本写的就是这个）
  return std::filesystem::path(apk_path.string() + ".json");
}

AppRelease::Stamp AppRelease::StatOf(const std::filesystem::path& path) {
  Stamp stamp;
  if (path.empty()) {
    return stamp;
  }
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) || ec) {
    return stamp;
  }
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) {
    return stamp;
  }
  const auto mtime = std::filesystem::last_write_time(path, ec);
  if (ec) {
    return stamp;
  }
  stamp.exists = true;
  stamp.size = static_cast<std::uint64_t>(size);
  stamp.mtime = mtime;
  return stamp;
}

std::optional<std::filesystem::path> AppRelease::ResolvePath() const {
  if (!StatOf(apk_path_).exists) {
    return std::nullopt;
  }
  return apk_path_;
}

AppReleaseInfo AppRelease::Describe() {
  const Stamp apk = StatOf(apk_path_);
  const Stamp manifest = StatOf(manifest_path_);

  const std::lock_guard<std::mutex> lock(mutex_);
  const bool same_apk = apk.exists == cached_apk_.exists && apk.size == cached_apk_.size &&
                        SameStamp(apk.mtime, cached_apk_.mtime);
  const bool same_manifest = manifest.exists == cached_manifest_.exists &&
                             manifest.size == cached_manifest_.size &&
                             SameStamp(manifest.mtime, cached_manifest_.mtime);
  if (cache_valid_ && same_apk && same_manifest) {
    return cached_;
  }

  AppReleaseInfo info;
  cached_apk_ = apk;
  cached_manifest_ = manifest;
  cache_valid_ = true;
  if (!apk.exists) {
    cached_ = info;  // available = false
    return info;
  }

  info.available = true;
  info.file_name = apk_path_.filename().string();
  // 大小与时间以**真实文件**为准：构建脚本换了包，这里立刻跟着变
  info.size_bytes = apk.size;
  info.built_at = ToIso8601Utc(ToTimeT(apk.mtime));

  if (manifest.exists) {
    std::ifstream input(manifest_path_, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const auto parsed = nlohmann::json::parse(buffer.str(), nullptr, false);
    if (!parsed.is_discarded() && parsed.is_object()) {
      // ⛔ 只有侧车与磁盘上这个包**对得上**（字节数一致）才采信它。
      // 实测踩到过：手换包不换侧车 → 大小是新的、版本与时间还是旧的，卡片自相矛盾。
      // 对不上就整体忽略侧车（版本号空着、时间用文件时间），并留一条日志。
      const std::uint64_t manifest_size = ManifestBytes(parsed, "size_bytes");
      if (manifest_size == apk.size) {
        info.version_name = ManifestString(parsed, "version_name");
        info.version_code = ManifestUint(parsed, "version_code");
        // 构建时间用侧车里的（那是 Gradle 真正出包的时刻）；侧车没有就保留文件时间
        const std::string manifest_built_at = ManifestString(parsed, "built_at");
        if (!manifest_built_at.empty()) {
          info.built_at = manifest_built_at;
        }
      } else {
        spdlog::warn(
            "安卓安装包侧车与安装包对不上，已忽略侧车（版本号会空着）| apk={} 字节 | sidecar={} "
            "字节",
            apk.size, manifest_size);
      }
    } else {
      spdlog::warn("安卓安装包侧车文件解析失败（忽略，按只有 APK 处理）| path={}",
                   manifest_path_.string());
    }
  }

  // SHA-256 一律现算（只在这个包变化时算一次）：侧车里那份可能对应的是别的包
  if (const auto digest = Sha256OfFile(apk_path_.string()); digest.Ok()) {
    info.sha256 = digest.Value().sha256_hex;
  } else {
    spdlog::warn("计算安卓安装包 SHA-256 失败（忽略）| path={}", apk_path_.string());
  }

  cached_ = info;
  return info;
}

}  // namespace lan_file_hub
