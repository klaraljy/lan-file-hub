// CORS 与安全响应头（阶段十二第 11、16 条）。
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace lan_file_hub {

// 判定请求来源是否在 CORS 白名单内。
// 配置层已经禁止 "*"；这里再做一次兜底，并对 scheme/host 做大小写不敏感比较
// （Origin 头由浏览器给出，大小写与尾部斜杠都可能不同）。
[[nodiscard]] bool IsOriginAllowed(const std::vector<std::string>& allowed_origins,
                                   std::string_view origin);

// 按请求路径给出合适的 CSP：
// - 网页路径（后端自托管前端：`/`、`/assets/*`、SPA 路由）：必须放行页面自己的脚本与样式，
//   否则浏览器直接白屏 —— 2026-10-02 实测，`default-src 'none'` 把 `/assets/index-*.js`
//   与 `/favicon.ico` 一起拦掉，HTTP 全是 200 但页面什么都没有；
// - 数据接口（`/api`、`/health`）：保持最严格的 `default-src 'none'`。
[[nodiscard]] std::string BuildContentSecurityPolicy(std::string_view request_path);

// 注册 CORS 与安全响应头：
// - 预检 OPTIONS 在路由之前直接应答（204），避免落到未注册的 OPTIONS 路由上变成 404；
// - 只有命中白名单的 Origin 才会收到 CORS 头，绝不使用通配；
// - 所有响应补安全头：禁止猜类型、禁止被内嵌、不泄露 Referer；
// - HSTS 只在真的走 TLS 时才发（明文 HTTP 下发 HSTS 会把站点锁死）。
void RegisterSecurityAdvices(const std::vector<std::string>& allowed_origins);

}  // namespace lan_file_hub
