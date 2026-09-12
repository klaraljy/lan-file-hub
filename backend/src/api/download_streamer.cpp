// Drogon 会带进 windows.h，而其中的 min/max 宏会破坏 std::max —— 必须在任何 include 之前定义
#ifdef _WIN32
#define NOMINMAX
#endif

#include "lan_file_hub/api/download_streamer.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <utility>
#include <vector>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

namespace lan_file_hub {
namespace {

// 每次读盘的粒度：够大以摊薄系统调用，够小以让限速粒度可接受
constexpr std::size_t kStreamChunkBytes = 256 * 1024;

// 连接在这么久之内一个字节都没能写到内核（说明客户端已经不读了，可能已断开），
// 就停止传输。没有这道闸，被丢弃的客户端会让服务端一直读盘并把数据堆在内存里。
constexpr std::chrono::seconds kStallTimeout{10};

// 一次下载的发送器：持有文件句柄与流，按令牌桶节奏推进。
// 全程在事件循环线程上跑（Drogon 的回调与定时器都在同一个 loop），因此不需要加锁。
class ThrottledFileSender : public std::enable_shared_from_this<ThrottledFileSender> {
 public:
  ThrottledFileSender(std::shared_ptr<ByteRateLimiter> limiter, std::string rate_key,
                      std::string path, std::uint64_t offset, std::uint64_t length,
                      std::weak_ptr<trantor::TcpConnection> connection, std::string log_key)
      : limiter_(std::move(limiter)),
        rate_key_(std::move(rate_key)),
        path_(std::move(path)),
        log_key_(std::move(log_key)),
        remaining_(length),
        connection_(std::move(connection)) {
    file_.open(path_, std::ios::binary);
    if (!file_.is_open()) {
      spdlog::error("限速下载无法打开对象文件 | key={}", log_key_);
      remaining_ = 0;
      failed_ = true;
      return;
    }
    if (offset > 0) {
      file_.seekg(static_cast<std::streamoff>(offset));
      if (!file_) {
        spdlog::error("限速下载无法定位对象偏移 | key={} | offset={}", log_key_, offset);
        remaining_ = 0;
        failed_ = true;
        return;
      }
    }
    // 基线：keep-alive 连接上此前已经发过一些字节，只看增量
    if (const auto peer = connection_.lock()) {
      last_socket_bytes_ = peer->bytesSent();
    }
  }

  // Drogon 的回调：第一次调用会移交流的持有权；结束或中断时以 nullptr 通知清理。
  void Pump(drogon::ResponseStreamPtr stream) {
    if (!stream) {
      if (!done_) {
        // 客户端断开的两条路径都要留痕：这条是 Drogon 主动通知，另一条是 send() 返回 false
        spdlog::info("限速下载被客户端中断（连接已关闭通知） | key={} | sent={} | remaining={}",
                     rate_key_, sent_, remaining_);
        done_ = true;
      }
      cancelled_ = true;
      stream_.reset();
      return;
    }
    if (!stream_) {
      stream_ = std::move(stream);
    }
    Drive();
  }

 private:
  // 连接已经断开（或连接对象已销毁）——限速传输必须据此停止
  [[nodiscard]] bool PeerGone() const {
    const auto connection = connection_.lock();
    return !connection || !connection->connected() || connection->disconnected();
  }

  // 客户端还在读吗？依据是连接真正写到内核的字节数有没有增长。
  // 不能只看 stream_->send() 的返回值：连接已断时它仍然可能返回 true（只是进了缓冲）。
  [[nodiscard]] bool Stalled() {
    const auto connection = connection_.lock();
    if (!connection) {
      return false;  // 连接对象没了：交给 PeerGone() 处理
    }
    const auto now = std::chrono::steady_clock::now();
    const std::size_t sent_now = connection->bytesSent();
    if (sent_now != last_socket_bytes_) {
      last_socket_bytes_ = sent_now;
      last_socket_progress_ = now;
      return false;
    }
    return (now - last_socket_progress_) > kStallTimeout;
  }

  void Drive() {
    if (driving_ || cancelled_ || done_ || !stream_) {
      return;
    }
    driving_ = true;
    std::vector<char> buffer(kStreamChunkBytes);

    while (true) {
      if (remaining_ == 0) {
        Finish();
        driving_ = false;
        return;
      }

      // 客户端断开或已经不再读取：两者都必须停下来
      if (PeerGone()) {
        spdlog::info("限速下载被客户端中断（连接已断开） | key={} | sent={} | remaining={}",
                     rate_key_, sent_, remaining_);
        Abort();
        driving_ = false;
        return;
      }
      if (Stalled()) {
        spdlog::warn("限速下载被中断（客户端 {} 秒未读取） | key={} | sent={} | remaining={}",
                     kStallTimeout.count(), rate_key_, sent_, remaining_);
        Abort();
        driving_ = false;
        return;
      }

      const auto want =
          static_cast<std::size_t>(std::min<std::uint64_t>(remaining_, buffer.size()));
      const auto grant = limiter_->Acquire(rate_key_, want);
      if (grant.bytes == 0) {
        // 令牌不够：交给定时器，绝不 sleep 阻塞事件循环
        ScheduleRetry(grant.retry_after);
        driving_ = false;
        return;
      }

      file_.read(buffer.data(), static_cast<std::streamsize>(grant.bytes));
      const auto got = static_cast<std::size_t>(file_.gcount());
      if (got == 0 || got < grant.bytes) {
        // 对象比元数据短：不能静默少发，客户端会收到"成功但不完整"的文件
        spdlog::error("限速下载中断：对象提前结束 | key={} | remaining={}", log_key_, remaining_);
        Abort();
        driving_ = false;
        return;
      }

      remaining_ -= got;
      sent_ += got;
      if (!stream_->send(std::string(buffer.data(), got))) {
        // 客户端断开：立刻停止读盘，不再为没人要的数据消耗磁盘与令牌
        spdlog::info("限速下载被客户端中断 | key={} | sent={} | remaining={}", rate_key_, sent_,
                     remaining_);
        Abort();
        driving_ = false;
        return;
      }
    }
  }

  void ScheduleRetry(std::chrono::milliseconds delay) {
    const double seconds = std::max(0.001, static_cast<double>(delay.count()) / 1000.0);
    auto self = shared_from_this();
    drogon::app().getLoop()->runAfter(seconds, [self]() { self->Resume(); });
  }

  void Resume() {
    if (cancelled_ || done_) {
      return;
    }
    Drive();
  }

  void Finish() {
    done_ = true;
    if (stream_) {
      stream_->close();
      stream_.reset();
    }
    spdlog::debug("限速下载完成 | key={} | bytes={}", rate_key_, sent_);
  }

  void Abort() {
    done_ = true;
    stream_.reset();  // ~ResponseStream 会关闭底层流
  }

  std::shared_ptr<ByteRateLimiter> limiter_;
  std::string rate_key_;
  std::string path_;
  std::string log_key_;  // 只用于日志：对象 Key，不记录绝对路径
  std::ifstream file_;
  std::uint64_t remaining_{0};
  std::uint64_t sent_{0};
  drogon::ResponseStreamPtr stream_;
  std::weak_ptr<trantor::TcpConnection> connection_;
  std::size_t last_socket_bytes_{0};
  std::chrono::steady_clock::time_point last_socket_progress_{std::chrono::steady_clock::now()};
  bool driving_{false};
  bool cancelled_{false};
  bool done_{false};
  bool failed_{false};
};

}  // namespace

drogon::HttpResponsePtr MakeThrottledFileResponse(std::shared_ptr<ByteRateLimiter> limiter,
                                                  std::string rate_key, std::string absolute_path,
                                                  std::uint64_t offset, std::uint64_t length,
                                                  std::string mime_type,
                                                  std::weak_ptr<trantor::TcpConnection> connection,
                                                  std::string log_key) {
  auto sender = std::make_shared<ThrottledFileSender>(std::move(limiter), std::move(rate_key),
                                                      std::move(absolute_path), offset, length,
                                                      std::move(connection), std::move(log_key));
  auto response = drogon::HttpResponse::newAsyncStreamResponse(
      [sender](drogon::ResponseStreamPtr stream) { sender->Pump(std::move(stream)); },
      true /* 限速下载可能一秒内都发不出第一个字节，必须关掉 kickoff 超时 */);
  response->setContentTypeCodeAndCustomString(drogon::CT_CUSTOM, mime_type);
  return response;
}

}  // namespace lan_file_hub
