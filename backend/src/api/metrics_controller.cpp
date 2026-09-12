#include "api/metrics_controller.h"

#include <string>

#include <drogon/drogon.h>

#include "lan_file_hub/common/metrics.h"

namespace lan_file_hub {

void MetricsController::Prometheus(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  (void)req;
  auto response = drogon::HttpResponse::newHttpResponse();
  response->setStatusCode(drogon::k200OK);
  response->setContentTypeCodeAndCustomString(drogon::CT_CUSTOM,
                                              "text/plain; version=0.0.4; charset=utf-8");
  response->setBody(Metrics().Render());
  callback(response);
}

}  // namespace lan_file_hub
