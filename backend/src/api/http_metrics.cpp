#include "api/http_metrics.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include <drogon/drogon.h>

#include "lan_file_hub/common/metrics.h"

namespace lan_file_hub {
namespace {

// 开始时间通过请求头带过来：属性表是 shared_ptr，GCC 的 -Wnull-dereference 会对
// `req->attributes()->get<T>()` 误报（MSVC 不报，但 Linux 构建 -Werror 会直接失败）。
constexpr char kStartHeader[] = "x-lfh-metrics-start-us";
// 路由模板最长也就几十个字符；异常长的一律归档，避免标签值失控
constexpr std::size_t kMaxRouteLabelLength = 96;

std::string MethodLabel(const drogon::HttpRequestPtr& req) {
  const char* raw = req->methodString();
  const std::string_view method = raw == nullptr ? std::string_view{} : std::string_view{raw};
  constexpr std::array<std::string_view, 7> kKnown{"GET",    "POST", "PUT",    "PATCH",
                                                   "DELETE", "HEAD", "OPTIONS"};
  for (const auto candidate : kKnown) {
    if (method == candidate) {
      return std::string(candidate);
    }
  }
  return "OTHER";
}

std::string RouteLabel(const drogon::HttpRequestPtr& req) {
  const std::string pattern(req->getMatchedPathPattern());
  if (pattern.empty()) {
    return "unmatched";
  }
  if (pattern.size() > kMaxRouteLabelLength) {
    return "long_path";
  }
  return pattern;
}

}  // namespace

std::string DownloadFailureReason(std::string_view error_code) {
  if (error_code == "RESOURCE_NOT_FOUND" || error_code == "FILE_NOT_FOUND" ||
      error_code == "FOLDER_NOT_FOUND" || error_code == "UPLOAD_NOT_FOUND") {
    return "not_found";
  }
  if (error_code == "FORBIDDEN" || error_code == "UNAUTHENTICATED" ||
      error_code == "TOKEN_EXPIRED") {
    return "forbidden";
  }
  if (error_code == "INVALID_ARGUMENT" || error_code == "CHUNK_INVALID" ||
      error_code == "UPLOAD_INCOMPLETE") {
    return "invalid_request";
  }
  if (error_code == "STORAGE_FULL" || error_code == "STORAGE_UNAVAILABLE" ||
      error_code == "STORAGE_BUSY") {
    return "storage";
  }
  return "other";
}

void RegisterHttpMetrics() {
  // 1) 路由前：只记开始时间（不短路，继续正常路由）
  drogon::app().registerPreRoutingAdvice([](const drogon::HttpRequestPtr& req,
                                            drogon::AdviceCallback&& callback,
                                            drogon::AdviceChainCallback&& chain) {
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    req->addHeader(std::string(kStartHeader), std::to_string(now));
    chain();
    (void)callback;
  });

  // 2) 发送前：按 method/route/status 记账并观测耗时
  drogon::app().registerPreSendingAdvice([](const drogon::HttpRequestPtr& req,
                                            const drogon::HttpResponsePtr& response) {
    const std::string method = MethodLabel(req);
    const std::string route = RouteLabel(req);
    const std::string status = std::to_string(static_cast<int>(response->getStatusCode()));

    Metrics().IncrementCounter("lan_file_hub_http_requests_total", "按方法/路由/状态码统计的请求数",
                               {{"method", method}, {"route", route}, {"status", status}});

    const std::string start_header = req->getHeader(std::string(kStartHeader));
    std::int64_t start = 0;
    const auto* begin = start_header.data();
    const auto* end = start_header.data() + start_header.size();
    if (start_header.empty() || std::from_chars(begin, end, start).ec != std::errc{}) {
      return;  // 没埋到点（理论上不会发生）：不记耗时，但请求计数已经记了
    }
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    const double seconds = static_cast<double>(now - start) / 1'000'000.0;
    Metrics().ObserveHistogram("lan_file_hub_http_request_duration_seconds",
                               "HTTP 请求处理耗时（秒）", seconds,
                               {{"method", method}, {"route", route}});
  });
}

}  // namespace lan_file_hub
