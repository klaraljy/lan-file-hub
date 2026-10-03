#include "lan_file_hub/api/web_hosting.h"

#include <filesystem>
#include <optional>
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

// 请求路径看起来像"带扩展名的文件"（而不是 /files 这类前端路由）
bool LooksLikeFile(const std::string& path) {
  const std::size_t slash = path.find_last_of('/');
  return path.find('.', slash == std::string::npos ? 0 : slash) != std::string::npos;
}

// 把 URL 路径安全地映射到 WEB_ROOT 下的真实文件，按**二进制**返回。
//
// 为什么需要它（2026-10-03 用户实测）：Drogon 的静态托管只处理它认识的 MIME 类型，
// `.apk` 这类未知扩展名它直接不管，请求落到 default handler —— 如果那里一律回退 index.html，
// 用户就**从自己服务器下载不到自己的 APK**（实测 `GET /lan-file-hub.apk` 返回的是网页）。
//
// 安全：先规范化再确认落在 WEB_ROOT 之内（防 `../` 穿越），并且安全响应头里的
// `X-Content-Type-Options: nosniff` 保证它不会被当成 HTML 执行。
std::optional<drogon::HttpResponsePtr> TryServeRawFile(const std::filesystem::path& root,
                                                       const std::string& url_path) {
  std::string relative = url_path;
  while (!relative.empty() && relative.front() == '/') {
    relative.erase(relative.begin());
  }
  if (relative.empty()) {
    return std::nullopt;
  }
  std::error_code ec;
  const std::filesystem::path root_canonical = std::filesystem::weakly_canonical(root, ec);
  if (ec) {
    return std::nullopt;
  }
  const std::filesystem::path candidate =
      std::filesystem::weakly_canonical(root_canonical / relative, ec);
  if (ec) {
    return std::nullopt;
  }
  const std::string root_text = root_canonical.generic_string();
  const std::string candidate_text = candidate.generic_string();
  if (candidate_text.size() <= root_text.size() ||
      candidate_text.compare(0, root_text.size(), root_text) != 0) {
    return std::nullopt;  // 跑到 WEB_ROOT 外面去了
  }
  if (!std::filesystem::is_regular_file(candidate, ec) || ec) {
    return std::nullopt;
  }
  auto response = drogon::HttpResponse::newFileResponse(candidate.string());
  response->setContentTypeCode(drogon::CT_APPLICATION_OCTET_STREAM);
  response->addHeader("Cache-Control", "no-cache");
  return response;
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

  // 找不到文件时：后端路径照常 404 JSON；带扩展名的真实文件按二进制发；
  // 其余 GET 一律回退到 index.html（前端路由自己处理）
  const std::string index_path = index.string();
  const std::filesystem::path document_root = root;
  drogon::app().setDefaultHandler(
      [index_path, document_root](const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        if (req->method() != drogon::Get || IsBackendPath(req->path())) {
          // 用文档里的错误码（RESOURCE_NOT_FOUND → 404）。
          // 自造 code 会落到错误码表的兜底分支变成 500（实测踩到过）。
          callback(MakeErrorResponse(Error{"RESOURCE_NOT_FOUND", "接口或资源不存在"},
                                     CurrentRequestId()));
          return;
        }
        // ⛔ 先试"当文件发"：`.apk` 这类 Drogon 不认识的扩展名就靠这一步（否则会变成网页）
        if (LooksLikeFile(req->path())) {
          if (auto file_response = TryServeRawFile(document_root, req->path())) {
            callback(*file_response);
            return;
          }
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
