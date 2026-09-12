// request_id 中间件：为每个请求分配/接收 request_id，并回写到响应头。
// 用 Drogon 的 advice 机制而不是 HttpFilter：
//   - 覆盖全部请求（包括未匹配路由的 404），不依赖过滤器名字符串查找
//   - 规则见 docs/architecture.md：客户端 X-Request-Id 必须校验长度与格式
#pragma once

namespace lan_file_hub {

// 在 drogon::app().run() 之前调用。
void RegisterRequestIdMiddleware();

}  // namespace lan_file_hub
