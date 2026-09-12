// 限速下载：用异步 chunked 流按令牌桶的节奏发送文件区间（阶段十二第 4 条）。
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <drogon/HttpResponse.h>
#include <trantor/net/TcpConnection.h>

#include "lan_file_hub/common/byte_rate_limiter.h"

namespace lan_file_hub {

// 构造一个按令牌桶限速的下载响应。
//
// 只在限速启用（rate > 0）时使用：异步流必然是 chunked 传输，拿不到 Content-Length，
// 因此默认（不限速）路径仍然走 newFileResponse 的零拷贝 + Content-Length + Range 语义。
//
// 约束（见 docs/storage.md「下载限速」）：
// - 不阻塞事件循环：令牌不够时用定时器排队，不用 sleep；
// - 客户端断开时立即停止读盘。注意 trantor 的 send() 在连接已断后仍可能返回 true
//   （写入进了发送缓冲），所以必须同时用连接的存活状态判断，不能只信 send() 的返回值；
// - 令牌桶策略，突发额度 1 秒；log_key 是对象 Key（不是绝对路径），只用于日志——
//   docs/security.md §7 明确要求日志里不得出现绝对路径。
[[nodiscard]] drogon::HttpResponsePtr MakeThrottledFileResponse(
    std::shared_ptr<ByteRateLimiter> limiter, std::string rate_key, std::string absolute_path,
    std::uint64_t offset, std::uint64_t length, std::string mime_type,
    std::weak_ptr<trantor::TcpConnection> connection, std::string log_key);

}  // namespace lan_file_hub
