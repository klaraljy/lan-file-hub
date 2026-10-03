// 「配套手机应用」安装包的信息（用户 2026-10-03：「我的」页要有手机 App 下载入口）。
//
// 为什么单独做一个东西：APK 是**会变的**——每次 `scripts\build-android.ps1` 重新出包，
// 大小、时间、校验和都会变。用户的要求是"**改完就更新的**"：
// 如果版本/大小/时间由前端写死、或者启动时读一次缓存住，那重新构建之后网页上还是旧数字
// （除非重启后端）。所以这里**每次请求都看真实文件**：
//   - 大小与构建时间：直接取自 `lan-file-hub.apk`（构建脚本另外写一份同名 `.json` 侧车，
//     里面有版本号与 SHA-256；侧车缺失时退化成"只有大小与文件时间"，照样能用）；
//   - 文件没换（大小 + 修改时间都没变）就复用上次算好的结果，换了立刻重算。
//
// 它只读**服务器自己的发布目录**（`APP_APK_PATH`，便携包默认 `WEB_ROOT/downloads/`），
// 与用户文件、用户路径无关——所以不走 ObjectStorage（AGENTS.md §5 那条红线管的是用户文件）。
#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

namespace lan_file_hub {

struct AppReleaseInfo {
  bool available{false};
  std::string file_name;     // lan-file-hub-share.apk（给 Content-Disposition 用）
  std::string version_name;  // 来自侧车；没有侧车时为空串
  std::uint32_t version_code{0};
  std::uint64_t size_bytes{0};
  std::string built_at;  // UTC ISO8601："2026-10-03T09:01:18Z"
  std::string sha256;    // 侧车里没有就现算（只在文件变化时算一次）
  std::string download_path{"/api/v1/app/download"};
};

class AppRelease {
 public:
  // apk_path 为空 = 没配置：Describe() 如实回报 available=false，下载接口回 404 文案，不报 500。
  explicit AppRelease(std::filesystem::path apk_path);

  AppRelease(const AppRelease&) = delete;
  AppRelease& operator=(const AppRelease&) = delete;

  // 线程安全（Drogon 的多个 IO 线程会同时调它）。
  [[nodiscard]] AppReleaseInfo Describe();

  // 安装包真实存在时给出路径；否则 nullopt。
  [[nodiscard]] std::optional<std::filesystem::path> ResolvePath() const;

 private:
  struct Stamp {
    bool exists{false};
    std::uint64_t size{0};
    std::filesystem::file_time_type mtime{};
  };

  static Stamp StatOf(const std::filesystem::path& path);
  static std::filesystem::path ManifestPathFor(const std::filesystem::path& apk_path);

  std::filesystem::path apk_path_;
  std::filesystem::path manifest_path_;

  std::mutex mutex_;
  // 缓存：只认"apk + 侧车"这一对文件的（大小 + 修改时间）。任一变化就重算。
  bool cache_valid_{false};
  Stamp cached_apk_{};
  Stamp cached_manifest_{};
  AppReleaseInfo cached_;
};

}  // namespace lan_file_hub
