// 健康检查控制器。语义见 docs/architecture.md：
//   /health/live  —— 进程是否在运行，不依赖任何外部组件
//   /health/ready —— 必要依赖（PostgreSQL、Redis、对象存储）是否可用
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace lan_file_hub {

class HealthController : public drogon::HttpController<HealthController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(HealthController::Live, "/health/live", drogon::Get);
  ADD_METHOD_TO(HealthController::Ready, "/health/ready", drogon::Get);
  METHOD_LIST_END

  static void Live(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  static void Ready(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

}  // namespace lan_file_hub
