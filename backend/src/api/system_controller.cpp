#include "api/system_controller.h"

#include <string>

#include <nlohmann/json.hpp>

#include "lan_file_hub/api/error_response.h"
#include "lan_file_hub/common/net.h"
#include "lan_file_hub/common/request_id.h"

namespace lan_file_hub {

void SystemController::Network(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  // 每次请求都现算：用户换网络之后，这里立刻就是新地址（UDP 探测不发包，开销可忽略）
  const std::string lan_address = GetPrimaryLocalAddress();
  // 客户端是通过哪个 Host 找过来的（127.0.0.1 / 局域网 IP / 域名）：
  // 前端据此判断要不要把链接换成局域网地址；也给排查用（Host 是客户端自己发的，不是内部信息）
  const std::string request_host = req->getHeader("Host");

  callback(MakeSuccessResponse(nlohmann::json{{"lan_address", lan_address},
                                              {"detected", !lan_address.empty()},
                                              {"request_host", request_host}},
                               CurrentRequestId()));
}

}  // namespace lan_file_hub
