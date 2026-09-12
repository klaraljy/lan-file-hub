// 日志模块：统一初始化入口，业务代码只使用 LOG_* 宏或 spdlog 默认 logger。
// 禁止记录的内容见 docs/architecture.md（password / jwt / share_token / 完整文件内容）。
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <spdlog/sinks/sink.h>

#include "lan_file_hub/common/result.h"

namespace lan_file_hub {

struct LoggingOptions {
  std::string level{"info"};  // trace/debug/info/warn/error/critical/off
  std::string file;           // 空 = 只输出到控制台
  // 需要在写盘前替换掉的敏感串（数据库口令、JWT 密钥等）。
  // 这是**最后一道闸**：调用点自己也要脱敏，但不能指望每个调用点都记得。
  std::vector<std::string> secrets;
};

// 初始化全局 logger（控制台 + 可选滚动文件）。失败时返回错误，调用方必须终止启动。
[[nodiscard]] Status InitializeLogging(const LoggingOptions& options);

// 包一层"写盘前脱敏"的 sink。单独暴露是为了能对脱敏本身做单元测试。
[[nodiscard]] std::shared_ptr<spdlog::sinks::sink> MakeRedactingSink(
    std::shared_ptr<spdlog::sinks::sink> inner, std::vector<std::string> secrets);

// 刷盘并释放（进程退出前调用）。
void ShutdownLogging();

}  // namespace lan_file_hub
