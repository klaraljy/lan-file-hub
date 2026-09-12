// Prometheus 指标暴露端点（阶段十三）：GET /metrics
//
// 安全取舍：本端点**不做认证**（Prometheus 抓取端的常规做法），因此它只暴露聚合数字，
// 不含用户标识、文件名或路径；标签值来自固定的路由模板与错误码表。
// 需要隔离时用反向代理限制来源，或把 METRICS_ENABLED 设为 false。
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace lan_file_hub {

class MetricsController : public drogon::HttpController<MetricsController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(MetricsController::Prometheus, "/metrics", drogon::Get);
  METHOD_LIST_END

  static void Prometheus(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

}  // namespace lan_file_hub
