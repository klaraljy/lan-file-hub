#include "lan_file_hub/common/logging.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/details/log_msg_buffer.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "lan_file_hub/common/config.h"

namespace lan_file_hub {
namespace {

constexpr char kPattern[] = "%Y-%m-%dT%H:%M:%S.%e%z [%l] [t=%t] %v";
constexpr std::size_t kMaxLogFileBytes = 10U * 1024U * 1024U;  // 单文件 10 MiB
constexpr std::size_t kMaxLogFiles = 5;

std::shared_ptr<spdlog::logger> g_logger;

// 写盘前脱敏：包住真实 sink，payload 里出现敏感串就先替换再交给下层。
// 这样做是因为"每个调用点自己记得脱敏"靠不住——漏一个调用点就会把口令写进日志。
class RedactingSink : public spdlog::sinks::sink {
 public:
  RedactingSink(std::shared_ptr<spdlog::sinks::sink> inner, std::vector<std::string> secrets)
      : inner_(std::move(inner)), secrets_(std::move(secrets)) {}

  void log(const spdlog::details::log_msg& msg) override {
    if (secrets_.empty()) {
      inner_->log(msg);
      return;
    }
    spdlog::details::log_msg_buffer buffer(msg);
    const std::string text =
        RedactSecrets(std::string(buffer.payload.data(), buffer.payload.size()), secrets_);
    buffer.payload = spdlog::string_view_t(text.data(), text.size());
    inner_->log(buffer);
  }

  void flush() override { inner_->flush(); }

  void set_pattern(const std::string& pattern) override { inner_->set_pattern(pattern); }

  void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override {
    inner_->set_formatter(std::move(formatter));
  }

 private:
  std::shared_ptr<spdlog::sinks::sink> inner_;
  std::vector<std::string> secrets_;
};

}  // namespace

std::shared_ptr<spdlog::sinks::sink> MakeRedactingSink(std::shared_ptr<spdlog::sinks::sink> inner,
                                                       std::vector<std::string> secrets) {
  if (!inner || secrets.empty()) {
    return inner;  // 没有配置敏感串就不必多包一层
  }
  return std::make_shared<RedactingSink>(std::move(inner), std::move(secrets));
}

Status InitializeLogging(const LoggingOptions& options) {
  try {
    const auto wrap = [&options](spdlog::sink_ptr sink) -> spdlog::sink_ptr {
      if (options.secrets.empty()) {
        return sink;
      }
      return MakeRedactingSink(std::move(sink), options.secrets);
    };

    std::vector<spdlog::sink_ptr> sinks;

    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console->set_pattern(kPattern);
    sinks.push_back(wrap(std::move(console)));

    if (!options.file.empty()) {
      auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          options.file, kMaxLogFileBytes, kMaxLogFiles);
      file->set_pattern(kPattern);
      sinks.push_back(wrap(std::move(file)));
    }

    auto logger = std::make_shared<spdlog::logger>("lan_file_hub", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::from_str(options.level));
    logger->flush_on(spdlog::level::warn);

    spdlog::set_default_logger(logger);
    spdlog::set_level(logger->level());
    g_logger = std::move(logger);
    return Status::Success();
  } catch (const spdlog::spdlog_ex& ex) {
    // 注意：这里不能再用 logger（它没建成），只能走 stderr
    return Status(Error{"LOG_INIT_FAILED", "日志初始化失败", ex.what()});
  }
}

void ShutdownLogging() {
  if (g_logger) {
    g_logger->flush();
  }
  spdlog::shutdown();
  g_logger.reset();
}

}  // namespace lan_file_hub
