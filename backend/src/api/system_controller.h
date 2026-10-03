// 系统信息接口：告诉前端「现在该用哪个局域网地址」。
//   GET /api/v1/system/network   →  { lan_address, detected, request_host }
//
// 为什么必须放在服务端：用户会**切换网络**（校园网 ↔ 手机热点），地址变了以后，
// 前端如果还按"打开页面时"的地址拼分享链接，手机就打不开；而服务端每次请求现算，
// **不用重启**就能给出新地址（用户 2026-10-02 反馈的正是这个问题）。
// 需要登录（AuthFilter）—— 局域网地址不需要对未登录的人公开。
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace lan_file_hub {

class SystemController : public drogon::HttpController<SystemController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(SystemController::Network, "/api/v1/system/network", drogon::Get, "AuthFilter");
  METHOD_LIST_END

  void Network(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

}  // namespace lan_file_hub
