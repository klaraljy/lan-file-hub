#include "api/request_id_middleware.h"

#include <string>

#include <drogon/drogon.h>

#include "lan_file_hub/common/request_id.h"

namespace lan_file_hub {

void RegisterRequestIdMiddleware() {
  // 1) 路由前：确定本次请求的 request_id 并写入线程上下文
  drogon::app().registerPreRoutingAdvice([](const drogon::HttpRequestPtr& req,
                                            drogon::AdviceCallback&& callback,
                                            drogon::AdviceChainCallback&& chain) {
    const std::string request_id = ResolveRequestId(req->getHeader("X-Request-Id"));
    SetCurrentRequestId(request_id);
    req->attributes()->insert("request_id", request_id);
    chain();
    (void)callback;  // 不短路，继续走正常路由
  });

  // 2) 响应生成时：把 request_id 放进响应头，便于客户端与日志对账
  drogon::app().registerHttpResponseCreationAdvice([](const drogon::HttpResponsePtr& response) {
    const std::string& request_id = CurrentRequestId();
    if (!request_id.empty()) {
      response->addHeader("X-Request-Id", request_id);
    }
  });
}

}  // namespace lan_file_hub
