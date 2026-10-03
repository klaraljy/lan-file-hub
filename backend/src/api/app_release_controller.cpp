#include "api/app_release_controller.h"

#include <memory>
#include <optional>
#include <string>

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include "lan_file_hub/api/error_response.h"
#include "lan_file_hub/application/app_context.h"
#include "lan_file_hub/common/http_util.h"
#include "lan_file_hub/common/request_id.h"

namespace lan_file_hub {
namespace {

// 取发布信息；服务没装配好时按"没有安装包"处理（绝不让用户看到 500）
AppReleaseInfo CurrentRelease() {
  const auto context = GetAppContext();
  if (!context || !context->app_release) {
    return AppReleaseInfo{};
  }
  return context->app_release->Describe();
}

nlohmann::json ToJson(const AppReleaseInfo& info) {
  return nlohmann::json{{"available", info.available},
                        {"file_name", info.file_name},
                        {"version_name", info.version_name},
                        {"version_code", info.version_code},
                        {"size_bytes", info.size_bytes},
                        {"built_at", info.built_at},
                        {"sha256", info.sha256},
                        {"download_path", info.download_path}};
}

}  // namespace

void AppReleaseController::Describe(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  (void)req;
  callback(MakeSuccessResponse(ToJson(CurrentRelease()), CurrentRequestId()));
}

void AppReleaseController::Download(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
  (void)req;
  const auto context = GetAppContext();
  const auto path =
      (context && context->app_release) ? context->app_release->ResolvePath() : std::nullopt;
  if (!path) {
    // 错误码取自 docs/api.md 的表（自造 code 会落到兜底分支变成 500，实测踩到过）
    callback(MakeErrorResponse(Error{"RESOURCE_NOT_FOUND",
                                     "服务器上还没有安卓安装包：先在这台电脑上跑一次 "
                                     "scripts\\build-android.ps1（或把 APK 放到 downloads 目录）"},
                               CurrentRequestId()));
    return;
  }

  const AppReleaseInfo info = CurrentRelease();
  auto response = drogon::HttpResponse::newFileResponse(path->string());
  response->setContentTypeCode(drogon::CT_APPLICATION_OCTET_STREAM);
  // 文件名净化后进 Content-Disposition（与用户文件下载同一套处理）
  response->addHeader(
      "Content-Disposition",
      BuildContentDisposition(info.file_name.empty() ? "lan-file-hub.apk" : info.file_name));
  // 不要缓存：重新构建之后必须拿到新包，而不是浏览器里的旧副本
  response->addHeader("Cache-Control", "no-cache");
  callback(response);
}

}  // namespace lan_file_hub
