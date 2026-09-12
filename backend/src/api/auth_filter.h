// 认证过滤器。
//
// Drogon 通过**类名**查找过滤器（见 docs/architecture.md 的分层说明），
// 所以这里放在全局命名空间，控制器里写 "AuthFilter" 即可引用。
//
// 职责：解析 Authorization: Bearer <token> → 校验 JWT → 复查用户状态 → 注入身份。
// 被禁用用户在这里就被挡住，不会进入业务逻辑（阶段二要求 11）。
#pragma once

#include <drogon/HttpFilter.h>

class AuthFilter : public drogon::HttpFilter<AuthFilter> {
 public:
  void doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& fcb,
                drogon::FilterChainCallback&& fccb) override;
};
