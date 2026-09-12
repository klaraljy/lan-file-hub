// 测试用临时目录：构造时创建，析构时删除。只给测试用，不参与产品代码。
#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace lan_file_hub::testing {

class TempDir {
 public:
  explicit TempDir(const std::string& tag) {
    static std::atomic<unsigned int> counter{0};
    path_ = std::filesystem::temp_directory_path() /
            ("lan_file_hub_test_" + tag + "_" + std::to_string(counter.fetch_add(1)));
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
    std::filesystem::create_directories(path_, ec);
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir(TempDir&&) = delete;
  TempDir& operator=(TempDir&&) = delete;

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
  [[nodiscard]] std::filesystem::path File(const std::string& name) const { return path_ / name; }

  void WriteFile(const std::string& name, const std::string& content) const {
    std::ofstream out(File(name), std::ios::binary | std::ios::trunc);
    out << content;
  }

 private:
  std::filesystem::path path_;
};

}  // namespace lan_file_hub::testing
