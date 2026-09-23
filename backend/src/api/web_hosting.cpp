#include "lan_file_hub/api/web_hosting.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include "lan_file_hub/api/error_response.h"
#include "lan_file_hub/common/request_id.h"

namespace lan_file_hub {
namespace {

// 这些前缀永远属于后端接口，不能回退成前端页面
bool IsBackendPath(const std::string& path) {
  return path.rfind("/api/", 0) == 0 || path.rfind("/health", 0) == 0 ||
         path.rfind("/metrics", 0) == 0;
}

}  // namespace

Status RegisterWebHosting(const std::string& web_root) {
  if (web_root.empty()) {
    spdlog::info("未配置 WEB_ROOT：只提供 API（前端由 vite 或 nginx 托管）");
    return Status::Success();
  }

  std::error_code ec;
  const std::filesystem::path root = std::filesystem::absolute(web_root, ec);
  if (ec || !std::filesystem::is_directory(root, ec)) {
    return Status(Error{"INTERNAL_ERROR", "WEB_ROOT 指向的目录不存在: " + web_root});
  }
  const std::filesystem::path index = root / "index.html";
  if (!std::filesystem::is_regular_file(index, ec)) {
    return Status(Error{"INTERNAL_ERROR",
                        "WEB_ROOT 目录里没有 index.html（前端还没构建？）: " + root.string()});
  }

  // 静态资源（/assets/xxx.js 等）由 Drogon 直接从磁盘发
  drogon::app().setDocumentRoot(root.string());

  // 找不到文件时：后端路径照常 404 JSON，其它 GET 一律回退到 index.html（前端路由自己处理）
  const std::string index_path = index.string();
  drogon::app().setDefaultHandler(
      [index_path](const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        if (req->method() != drogon::Get || IsBackendPath(req->path())) {
          // 用文档里的错误码（RESOURCE_NOT_FOUND → 404）。
          // 自造 code 会落到错误码表的兜底分支变成 500（实测踩到过）。
          callback(MakeErrorResponse(Error{"RESOURCE_NOT_FOUND", "接口或资源不存在"},
                                     CurrentRequestId()));
          return;
        }
        auto response = drogon::HttpResponse::newFileResponse(index_path);
        response->setContentTypeCode(drogon::CT_TEXT_HTML);
        // 页面本身不要缓存：升级后刷新就能拿到新版本（带 hash 的静态资源仍可长缓存）
        response->addHeader("Cache-Control", "no-cache");
        callback(response);
      });

  spdlog::info("已启用前端托管 | root={} | 前端路由回退 index.html", root.string());
  return Status::Success();
}

}  // namespace lan_file_hub
