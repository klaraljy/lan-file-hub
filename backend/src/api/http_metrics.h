// HTTP 层指标：请求总数、响应延迟、按状态码分布（阶段十三第 7、12 条）。
//
// 关键约束：**标签值不能来自无限制的用户输入**。
// 因此 route 标签取 Drogon 的路由模板（`getMatchedPathPattern()`），未匹配的请求统一记为
// "unmatched"，method 只认白名单里的标准方法，其余记为 "OTHER"。
#pragma once

#include <string>
#include <string_view>

namespace lan_file_hub {

// 注册 HTTP 指标中间件（请求开始时埋点，响应发送前记账）
void RegisterHttpMetrics();

// 下载失败原因归档：把错误码映射到**封闭集合**里的原因标签，
// 绝不把错误码原文或用户输入直接当标签（基数会失控）。
[[nodiscard]] std::string DownloadFailureReason(std::string_view error_code);

}  // namespace lan_file_hub
