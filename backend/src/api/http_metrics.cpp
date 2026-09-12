#include "api/http_metrics.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include <drogon/drogon.h>

#include "lan_file_hub/common/metrics.h"

namespace lan_file_hub {
namespace {

constexpr char kStartKey[] = "metrics_start_us";
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
    req->attributes()->insert(kStartKey, static_cast<std::int64_t>(now));
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

    // 取一次属性表并显式判空：GCC 的 -Wnull-dereference 会盯着"两次解引用同一指针"的写法
    //（MSVC 不报，但 Linux 构建用 -Werror，必须写得没有歧义）
    const auto attributes = req->attributes();
    if (attributes == nullptr || !attributes->find(kStartKey)) {
      return;
    }
    const auto start = attributes->get<std::int64_t>(kStartKey);
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
