#include "api/trash_controller.h"

#include <string>
#include <utility>

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "lan_file_hub/api/error_response.h"
#include "lan_file_hub/application/app_context.h"
#include "lan_file_hub/common/pagination.h"
#include "lan_file_hub/common/request_id.h"

namespace lan_file_hub {
namespace {

nlohmann::json TrashItemToJson(const TrashItem& item) {
  return nlohmann::json{
      {"id", item.id},
      {"type", item.kind},
      {"name", item.name},
      {"size_bytes", item.size_bytes},
      {"original_folder_id", item.folder_id},
      {"original_folder_name", item.folder_name},
      {"deleted_at", item.deleted_at},
  };
}

// 排序白名单：只允许映射到固定 SQL 片段（docs/api.md）
constexpr char kDefaultOrder[] = "deleted_at DESC, name ASC";

Actor ActorFrom(const drogon::HttpRequestPtr& req) {
  Actor actor;
  actor.user_id = req->attributes()->get<std::string>("user_id");
  const std::string role = req->attributes()->get<std::string>("user_role");
  actor.role = (role == "ADMIN") ? UserRole::kAdmin : UserRole::kUser;
  return actor;
}

void RespondError(const std::function<void(const drogon::HttpResponsePtr&)>& callback,
                  const Error& error) {
  if (!error.detail.empty()) {
    spdlog::warn("回收站请求失败 | code={} | detail={}", error.code,
                 RedactWithContext(error.detail));
  }
  callback(MakeErrorResponse(error, CurrentRequestId()));
}

}  // namespace

void TrashController::List(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto pagination = ParsePagination(req->getParameter("page"), req->getParameter("page_size"),
                                    req->getParameter("sort_by"), req->getParameter("sort_order"));
  if (!pagination) {
    RespondError(callback, pagination.GetError());
    return;
  }

  auto listing =
      context->trash_service->List(ActorFrom(req), pagination.Value(), req->getParameter("type"));
  if (!listing) {
    RespondError(callback, listing.GetError());
    return;
  }

  nlohmann::json items = nlohmann::json::array();
  for (const auto& item : listing.Value().items) {
    items.push_back(TrashItemToJson(item));
  }
  callback(MakeSuccessResponse(
      nlohmann::json{
          {"items", std::move(items)},
          {"total", listing.Value().total},
          {"page", listing.Value().page},
          {"page_size", listing.Value().page_size},
          {"retention_days", context->trash_service->retention_seconds() / 86400},
      },
      CurrentRequestId()));
}

void TrashController::Restore(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              std::string resource_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto outcome = context->trash_service->Restore(ActorFrom(req), resource_id);
  if (!outcome) {
    RespondError(callback, outcome.GetError());
    return;
  }

  const RestoreOutcome& restored = outcome.Value();
  callback(MakeSuccessResponse(
      nlohmann::json{
          {"id", restored.item.id},
          {"type", restored.item.kind},
          {"name", restored.item.name},
          {"target_folder_id", restored.target_folder_id},
          {"moved_to_root", restored.moved_to_root},
          {"already_restored", restored.already_restored},
      },
      CurrentRequestId()));
}

void TrashController::Purge(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            std::string resource_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto outcome = context->trash_service->Purge(ActorFrom(req), resource_id);
  if (!outcome) {
    RespondError(callback, outcome.GetError());
    return;
  }

  const PurgeOutcome& purged = outcome.Value();
  callback(MakeSuccessResponse(
      nlohmann::json{
          {"id", purged.resource_id},
          {"type", purged.kind},
          {"freed_bytes", purged.freed_bytes},
          {"file_count", purged.file_count},
          {"folder_count", purged.folder_count},
          // 对象由后台任务异步删除，这里只报告已入队的数量
          {"queued_objects", purged.queued_objects},
          {"async", true},
      },
      CurrentRequestId()));
}

}  // namespace lan_file_hub
