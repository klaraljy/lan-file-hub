// 在线状态接口（五阶段改造 ⑤：广场 / 在线用户，用户 2026-09-21 指示）。
//
//   POST /api/v1/presence/heartbeat   心跳（客户端每 30 秒一次）：上报设备信息，返回在线列表与我的设备
//   GET  /api/v1/presence/online      只取在线列表（广场用）
//   PUT  /api/v1/presence/visibility  设置「是否让别人看到我在线」（默认可见）
//
// 隐私口径：隐身用户不会出现在**别人**的在线列表里；自己始终能看到自己的设备。
// 在线状态保存在进程内存（见 application/presence_registry.h 的设计说明），因此**不落库、重启即清空**。
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace lan_file_hub {

class PresenceController : public drogon::HttpController<PresenceController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(PresenceController::Heartbeat, "/api/v1/presence/heartbeat", drogon::Post,
                "AuthFilter");
  ADD_METHOD_TO(PresenceController::Online, "/api/v1/presence/online", drogon::Get, "AuthFilter");
  ADD_METHOD_TO(PresenceController::SetVisibility, "/api/v1/presence/visibility", drogon::Put,
                "AuthFilter");
  METHOD_LIST_END

  void Heartbeat(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  void Online(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback);

  void SetVisibility(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

}  // namespace lan_file_hub
