#include "api/folder_controller.h"

#include <string>
#include <utility>

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "lan_file_hub/api/error_response.h"
#include "lan_file_hub/api/json_body.h"
#include "lan_file_hub/application/app_context.h"
#include "lan_file_hub/common/pagination.h"
#include "lan_file_hub/common/request_id.h"

namespace lan_file_hub {
namespace {

// 对外的文件夹表示。不暴露 path_key（内部实现细节）。
nlohmann::json FolderToJson(const Folder& folder) {
  nlohmann::json json{
      {"id", folder.id},
      {"owner_id", folder.owner_id},
      {"name", folder.name},
      {"status", ToDbString(folder.status)},
      {"is_root", folder.IsRoot()},
      {"created_at", folder.created_at},
      {"updated_at", folder.updated_at},
  };
  json["parent_id"] =
      folder.parent_id.has_value() ? nlohmann::json(*folder.parent_id) : nlohmann::json(nullptr);
  json["deleted_at"] =
      folder.deleted_at.has_value() ? nlohmann::json(*folder.deleted_at) : nlohmann::json(nullptr);
  return json;
}

std::string Param(const drogon::HttpRequestPtr& req, const char* name) {
  return req->getParameter(name);
}

// 从认证过滤器注入的属性里取执行者
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
    spdlog::warn("文件夹请求失败 | code={} | detail={}", error.code,
                 RedactWithContext(error.detail));
  }
  callback(MakeErrorResponse(error, CurrentRequestId()));
}

}  // namespace

void FolderController::Root(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }
  auto root = context->folders_service->EnsureRootFolder(ActorFrom(req));
  if (!root) {
    RespondError(callback, root.GetError());
    return;
  }
  callback(MakeSuccessResponse(FolderToJson(root.Value()), CurrentRequestId()));
}

void FolderController::Children(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                std::string folder_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto pagination = ParsePagination(Param(req, "page"), Param(req, "page_size"),
                                    Param(req, "sort_by"), Param(req, "sort_order"));
  if (!pagination) {
    RespondError(callback, pagination.GetError());
    return;
  }

  auto listing =
      context->folders_service->ListChildren(ActorFrom(req), folder_id, pagination.Value());
  if (!listing) {
    RespondError(callback, listing.GetError());
    return;
  }

  nlohmann::json items = nlohmann::json::array();
  for (const auto& folder : listing.Value().folders) {
    items.push_back(FolderToJson(folder));
  }

  nlohmann::json payload{
      {"items", std::move(items)},
      {"total", listing.Value().total},
      {"page", listing.Value().page},
      {"page_size", listing.Value().page_size},
  };
  callback(MakeSuccessResponse(payload, CurrentRequestId()));
}

// 「一同下载文件夹里的东西」（2026-10-03 用户要求）：返回该目录**含子目录**的全部文件清单。
// 只给 id / 路径 / 大小，内容仍走既有的"一次性票据 + 浏览器原生下载"逐个下 —— 不打包。
void FolderController::Files(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             std::string folder_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }
  auto listing = context->folders_service->ListFilesRecursive(ActorFrom(req), folder_id);
  if (!listing) {
    RespondError(callback, listing.GetError());
    return;
  }

  nlohmann::json items = nlohmann::json::array();
  for (const auto& entry : listing.Value().items) {
    items.push_back(nlohmann::json{
        {"file_id", entry.file_id},
        {"name", entry.name},
        {"path", entry.path},
        {"size_bytes", entry.size_bytes},
    });
  }
  nlohmann::json payload{
      {"items", std::move(items)},
      {"file_count", listing.Value().file_count},
      {"total_bytes", listing.Value().total_bytes},
      {"truncated", listing.Value().truncated},
  };
  callback(MakeSuccessResponse(payload, CurrentRequestId()));
}

void FolderController::Path(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            std::string folder_id) {  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }
  auto chain = context->folders_service->GetPath(ActorFrom(req), folder_id);
  if (!chain) {
    RespondError(callback, chain.GetError());
    return;
  }
  nlohmann::json items = nlohmann::json::array();
  for (const auto& folder : chain.Value()) {
    items.push_back(FolderToJson(folder));
  }
  callback(MakeSuccessResponse(nlohmann::json{{"items", std::move(items)}}, CurrentRequestId()));
}

void FolderController::Detail(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              std::string folder_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }
  auto folder = context->folders_service->Get(ActorFrom(req), folder_id);
  if (!folder) {
    RespondError(callback, folder.GetError());
    return;
  }
  callback(MakeSuccessResponse(FolderToJson(folder.Value()), CurrentRequestId()));
}

void FolderController::Create(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto body = ParseJsonBody(req);
  if (!body) {
    RespondError(callback, body.GetError());
    return;
  }
  const auto& json = body.Value();
  if (!json.contains("parent_id") || !json["parent_id"].is_string()) {
    RespondError(callback, Error{"INVALID_ARGUMENT", "缺少 parent_id"});
    return;
  }
  if (!json.contains("name") || !json["name"].is_string()) {
    RespondError(callback, Error{"INVALID_ARGUMENT", "缺少 name"});
    return;
  }

  auto created = context->folders_service->Create(
      ActorFrom(req), json["parent_id"].get<std::string>(), json["name"].get<std::string>());
  if (!created) {
    RespondError(callback, created.GetError());
    return;
  }
  callback(MakeSuccessResponse(FolderToJson(created.Value()), CurrentRequestId()));
}

void FolderController::Update(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              std::string folder_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }

  auto body = ParseJsonBody(req);
  if (!body) {
    RespondError(callback, body.GetError());
    return;
  }
  const auto& json = body.Value();
  const bool has_name = json.contains("name") && json["name"].is_string();
  const bool has_parent = json.contains("parent_id") && json["parent_id"].is_string();

  if (!has_name && !has_parent) {
    RespondError(callback, Error{"INVALID_ARGUMENT", "需要提供 name 或 parent_id"});
    return;
  }
  if (has_name && has_parent) {
    // 一次只做一件事，避免「改名成功但移动失败」这种半成品状态
    RespondError(callback, Error{"INVALID_ARGUMENT", "一次只能重命名或移动其中一项"});
    return;
  }

  const Actor actor = ActorFrom(req);
  if (has_name) {
    auto renamed =
        context->folders_service->Rename(actor, folder_id, json["name"].get<std::string>());
    if (!renamed) {
      RespondError(callback, renamed.GetError());
      return;
    }
    callback(MakeSuccessResponse(FolderToJson(renamed.Value()), CurrentRequestId()));
    return;
  }

  auto moved =
      context->folders_service->Move(actor, folder_id, json["parent_id"].get<std::string>());
  if (!moved) {
    RespondError(callback, moved.GetError());
    return;
  }
  callback(MakeSuccessResponse(FolderToJson(moved.Value()), CurrentRequestId()));
}

void FolderController::Remove(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              std::string folder_id) {
  const auto context = GetAppContext();
  if (!context) {
    RespondError(callback, Error{"INTERNAL_ERROR", "服务未初始化"});
    return;
  }
  auto status = context->folders_service->SoftDelete(ActorFrom(req), folder_id);
  if (!status.Ok()) {
    RespondError(callback, status.GetError());
    return;
  }
  callback(MakeSuccessResponse(nlohmann::json{{"deleted", true}, {"folder_id", folder_id}},
                               CurrentRequestId()));
}

}  // namespace lan_file_hub
