// 前端静态托管（五阶段改造 ④：便携包的单端口形态）。
//
// 为什么要它：便携包里不想再拖一个 Node.js（vite）或 nginx，所以让后端自己托管前端 dist：
//   - 静态资源（/assets/*.js 等）交给 Drogon 的 documentRoot；
//   - 前端路由（/files、/shared、/s/<token> …）在找不到文件时**回退到 index.html**（SPA 必需）；
//   - /api/、/health、/metrics 这些后端路径**绝不回退**，否则前端会拿到 HTML 而不是 JSON 错误。
#pragma once

#include <string>

#include "lan_file_hub/common/result.h"

namespace lan_file_hub {

// 注册前端托管。web_root 为空 = 什么都不做（开发用 vite、生产用 nginx 都走这条路）。
// 目录不存在或缺少 index.html 时返回错误，由调用方（main）决定是否中止启动。
[[nodiscard]] Status RegisterWebHosting(const std::string& web_root);

}  // namespace lan_file_hub
