#include "api/health_controller.h"

#include <string>
#include <utility>

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include "lan_file_hub/common/metrics.h"
#include "lan_file_hub/common/request_id.h"
#include "lan_file_hub/infrastructure/health_check.h"

namespace lan_file_hub {
namespace {

// 自己序列化 JSON 并显式设置 Content-Type，不依赖 Drogon 的 JSON 绑定
drogon::HttpResponsePtr JsonResponse(const nlohmann::json& payload, drogon::HttpStatusCode status) {
  auto response = drogon::HttpResponse::newHttpResponse();
  response->setStatusCode(status);
  response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
  response->setBody(payload.dump());
  return response;
}

std::string CurrentRequestIdOrEmpty() {
  return CurrentRequestId();
}

}  // namespace

void HealthController::Live(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  (void)req;
  nlohmann::json payload{
      {"status", "ok"},
      {"service", "lan-file-hub"},
      {"check", "live"},
      {"request_id", CurrentRequestIdOrEmpty()},
  };
  callback(JsonResponse(payload, drogon::k200OK));
}

void HealthController::Ready(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  (void)req;
  const auto checker = GetHealthChecker();
  if (!checker) {
    nlohmann::json payload{
        {"status", "unavailable"},
        {"check", "ready"},
        {"reason", "健康检查器未初始化"},
        {"request_id", CurrentRequestIdOrEmpty()},
    };
    callback(JsonResponse(payload, drogon::k503ServiceUnavailable));
    return;
  }

  const HealthReport report = checker->CheckReadiness();

  nlohmann::json components = nlohmann::json::array();
  for (const auto& component : report.components) {
    // 健康检查指标（阶段十三第 8 条）：每次都刷新，Prometheus 抓取时能直接看到依赖状态。
    // dependency 标签来自固定的组件名（postgresql/redis/object_storage），基数有界。
    Metrics().SetGauge("lan_file_hub_health_status", "依赖组件健康状态（1=可用，0=不可用）",
                       component.ok ? 1.0 : 0.0, {{"dependency", component.name}});
    components.push_back(nlohmann::json{
        {"name", component.name},
        {"required", component.required},
        {"status", component.ok ? "ok" : "unavailable"},
        {"latency_ms", component.latency_ms},
        {"detail", component.detail},
    });
  }

  nlohmann::json payload{
      {"status", report.ready ? "ok" : "unavailable"},
      {"check", "ready"},
      {"components", std::move(components)},
      {"request_id", CurrentRequestIdOrEmpty()},
  };
  callback(JsonResponse(payload, report.ready ? drogon::k200OK : drogon::k503ServiceUnavailable));
}

}  // namespace lan_file_hub
