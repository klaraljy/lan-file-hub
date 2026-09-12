// 统一结果类型：业务预期错误用 Result 返回，不用异常（项目 AGENTS.md §5）。
#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace lan_file_hub {

// 统一错误。
// code 取自 docs/api.md 的错误码表；message 面向人；detail 只进日志，不得包含凭据。
// details 会随错误响应返回给客户端（例如版本冲突时的 current_version_id）：
// 里面只允许放 ID、版本号这类客户端本来就能看到的标识，不得放路径、SQL 或密钥。
struct Error {
  std::string code;
  std::string message;
  std::string detail;
  std::map<std::string, std::string> details;

  Error() = default;
  Error(std::string error_code, std::string error_message, std::string error_detail = {})
      : code(std::move(error_code)),
        message(std::move(error_message)),
        detail(std::move(error_detail)) {}

  // 带结构化附加信息的错误（如版本冲突要告诉客户端当前版本）
  static Error WithDetails(std::string error_code, std::string error_message,
                           std::map<std::string, std::string> extra) {
    Error error(std::move(error_code), std::move(error_message));
    error.details = std::move(extra);
    return error;
  }
};

// 成功/失败的返回值。
template <typename T>
class Result {
 public:
  Result(T value)
      : storage_(std::in_place_index<0>, std::move(value)) {
  }  // NOLINT(google-explicit-constructor)
  Result(Error error)
      : storage_(std::in_place_index<1>, std::move(error)) {
  }  // NOLINT(google-explicit-constructor)

  [[nodiscard]] bool Ok() const noexcept { return storage_.index() == 0; }
  explicit operator bool() const noexcept { return Ok(); }

  [[nodiscard]] const T& Value() const { return std::get<0>(storage_); }
  [[nodiscard]] T& Value() { return std::get<0>(storage_); }

  [[nodiscard]] const Error& GetError() const { return std::get<1>(storage_); }

 private:
  std::variant<T, Error> storage_;
};

inline Result<std::string> Ok(std::string value) {
  return {std::move(value)};
}

// 无返回值的操作：成功用 Status::Success()，失败用 Status(Error{...})。
class Status {
 public:
  Status() = default;
  explicit Status(Error error) : error_(std::move(error)), ok_(false) {}

  [[nodiscard]] static Status Success() { return {}; }
  [[nodiscard]] bool Ok() const noexcept { return ok_; }
  [[nodiscard]] const Error& GetError() const noexcept { return error_; }

 private:
  Error error_;
  bool ok_{true};
};

}  // namespace lan_file_hub
