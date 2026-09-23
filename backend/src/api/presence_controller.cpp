#include "api/presence_controller.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "lan_file_hub/api/error_response.h"
#include "lan_file_hub/api/json_body.h"
#include "lan_file_hub/application/app_context.h"
#include "lan_file_hub/application/presence_registry.h"
#include "lan_file_hub/common/request_id.h"

namespace lan_file_hub {
namespace {

constexpr std::size_t kMaxFieldLength = 64;

void RespondError(const std::function<void(const drogon::HttpResponsePtr&)>& callback,
                  const Error& error) {
  callback(MakeErrorResponse(error, CurrentRequestId()));
}

// 设备标识是客户端生成的，只允许安全字符（会进内存 key，不进 SQL 也不进路径）
bool IsSafeDeviceId(const std::string& value) {
  if (value.empty() || value.size() > kMaxFieldLength) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
    return std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '.';
  });
}

std::string Truncate(std::string value, std::size_t max_length = kMaxFieldLength) {
  if (value.size() > max_length) {
    value.resize(max_length);
  }
  return value;
}

// 别人看到的条目：只给"是谁、什么设备"，**不给设备 id**（最小暴露）
nlohmann::json PublicEntryToJson(const PresenceEntry& entry) {
  return nlohmann::json{
      {"user_id", entry.user_id},          {"username", entry.username},
      {"device_name", entry.device_name},  {"platform", entry.platform},
      {"last_seen", entry.last_seen_unix},
  };
}

// 自己的设备：带上 device_id，前端用它判断"哪台是我现在这台"
nlohmann::json OwnDeviceToJson(const PresenceEntry& entry) {
  nlohmann::json json = PublicEntryToJson(entry);
  json["device_id"] = entry.device_id;
  json["visible"] = entry.visible;
  return json;
}

// 心跳里"我是否可见"由 AuthFilter 放进属性（避免每个心跳再查一次库）。
// ⚠️ 属性是 std::any：**存什么类型就要按什么类型读**。存 const char* 再按 std::string 读
// 会取到空串（实测踩到，导致默认可见变成默认隐身）。取不到时按"默认可见"处理。
bool VisibleFrom(const drogon::HttpRequestPtr& req) {
  try {
    return req->attributes()->get<bool>("user_presence_visible");
  } catch (const std::exception&) {
    return true;  // 默认可见（用户要求：只要在线就默认显示，可在设置里隐藏）
  }
}

}  // namespace

void PresenceController::Heartbeat(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context || !context->presence) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto body = ParseJsonBody(req);
  if (!body) {
    RespondError(callback, body.GetError());
    return;
  }

  const std::string device_id = body.Value().value("device_id", std::string{});
  if (!IsSafeDeviceId(device_id)) {
    RespondError(callback, Error{"INVALID_ARGUMENT", "device_id 缺失或含非法字符"});
    return;
  }

  PresenceEntry entry;
  entry.user_id = req->attributes()->get<std::string>("user_id");
  entry.username = req->attributes()->get<std::string>("username");
  entry.device_id = device_id;
  entry.device_name = Truncate(body.Value().value("device_name", std::string{"未命名设备"}));
  entry.platform = Truncate(body.Value().value("platform", std::string{"web"}));
  entry.visible = VisibleFrom(req);

  const std::int64_t now = NowUnixSeconds();
  // 先留住 user_id：下面 Touch 会 move 走 entry，不能再从 entry 里读（踩过：读出来是空串，
  // 导致「我的设备」永远是空的）
  const std::string self_id = entry.user_id;
  const bool visible = entry.visible;
  context->presence->Touch(std::move(entry), now);
  const std::size_t swept = context->presence->Sweep(now);

  nlohmann::json online = nlohmann::json::array();
  for (const auto& other : context->presence->OnlineOthers(self_id, now)) {
    online.push_back(PublicEntryToJson(other));
  }
  nlohmann::json devices = nlohmann::json::array();
  for (const auto& device : context->presence->DevicesOf(self_id, now)) {
    devices.push_back(OwnDeviceToJson(device));
  }

  if (swept > 0) {
    spdlog::debug("在线状态清理 | 移除超时设备={}", swept);
  }
  callback(MakeSuccessResponse(nlohmann::json{{"visible", visible},
                                              {"online", std::move(online)},
                                              {"my_devices", std::move(devices)},
                                              {"ttl_seconds", kPresenceTtlSeconds}},
                               CurrentRequestId()));
}

void PresenceController::Online(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context || !context->presence) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  const std::string self_id = req->attributes()->get<std::string>("user_id");
  const std::int64_t now = NowUnixSeconds();
  // 顺手清理超时记录（Sweep 是 [[nodiscard]]，这里明确表示"不关心数量"）
  (void)context->presence->Sweep(now);

  nlohmann::json items = nlohmann::json::array();
  for (const auto& other : context->presence->OnlineOthers(self_id, now)) {
    items.push_back(PublicEntryToJson(other));
  }
  callback(MakeSuccessResponse(
      nlohmann::json{{"items", std::move(items)}, {"ttl_seconds", kPresenceTtlSeconds}},
      CurrentRequestId()));
}

void PresenceController::SetVisibility(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context || !context->presence) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto body = ParseJsonBody(req);
  if (!body) {
    RespondError(callback, body.GetError());
    return;
  }
  if (!body.Value().contains("visible") || !body.Value()["visible"].is_boolean()) {
    RespondError(callback, Error{"INVALID_ARGUMENT", "visible 必须是布尔值"});
    return;
  }

  const std::string user_id = req->attributes()->get<std::string>("user_id");
  const bool visible = body.Value()["visible"].get<bool>();
  const Status saved = context->users->SetPresenceVisible(user_id, visible);
  if (!saved.Ok()) {
    RespondError(callback, saved.GetError());
    return;
  }
  // 立刻对本进程内的记录生效（其它用户最多 30 秒后随自己的心跳看到新状态）
  context->presence->SetVisibility(user_id, visible);

  spdlog::info("在线可见性变更 | user={} | visible={}", user_id, visible);
  callback(MakeSuccessResponse(nlohmann::json{{"visible", visible}}, CurrentRequestId()));
}

}  // namespace lan_file_hub
