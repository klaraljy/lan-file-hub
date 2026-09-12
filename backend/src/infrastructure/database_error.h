// 数据库异常 → 统一错误（各 repository 共用，避免七份实现各自漂移）。
#pragma once

#include <string>
#include <utility>

#include <pqxx/except.hxx>

#include "lan_file_hub/common/result.h"

namespace lan_file_hub {

// SQLSTATE 22xxx（data_exception）是**客户端参数问题**，例如把非 UUID 文本当 uuid 用
// （22P02 invalid_text_representation）。这类错误必须报成 INVALID_ARGUMENT，
// 报成 STORAGE_UNAVAILABLE 会让调用方以为"服务繁忙、可以重试"，并把它当成服务故障。
// 23xxx（完整性约束）等其余数据库错误仍然按服务端故障处理。
inline Error DatabaseError(const std::exception& ex, std::string message) {
  if (dynamic_cast<const pqxx::data_exception*>(&ex) != nullptr) {
    return Error{"INVALID_ARGUMENT", "请求参数格式不合法", ex.what()};
  }
  // 异常文本可能带连接串；调用方会用 config 的 secrets 做二次脱敏
  return Error{"STORAGE_UNAVAILABLE", std::move(message), ex.what()};
}

}  // namespace lan_file_hub
