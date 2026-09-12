#include "lan_file_hub/common/pagination.h"

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

namespace lan_file_hub {
namespace {

Result<int> ParsePositiveInt(std::string_view text, const char* field, int fallback, int min_value,
                             int max_value) {
  if (text.empty()) {
    return fallback;
  }
  const std::string trimmed(text);
  if (trimmed.find_first_not_of("0123456789") != std::string::npos) {
    return Error{"INVALID_ARGUMENT", std::string(field) + " 必须是正整数"};
  }
  int value = 0;
  try {
    value = std::stoi(trimmed);
  } catch (const std::exception&) {
    return Error{"INVALID_ARGUMENT", std::string(field) + " 超出范围"};
  }
  if (value < min_value || value > max_value) {
    return Error{"INVALID_ARGUMENT", std::string(field) + " 必须在 " + std::to_string(min_value) +
                                         " 到 " + std::to_string(max_value) + " 之间"};
  }
  return value;
}

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

}  // namespace

Result<Pagination> ParsePagination(std::string_view page, std::string_view page_size,
                                   std::string_view sort_by, std::string_view sort_order) {
  Pagination result;

  auto parsed_page = ParsePositiveInt(page, "page", 1, 1, 1'000'000);
  if (!parsed_page)
    return parsed_page.GetError();
  result.page = parsed_page.Value();

  auto parsed_size =
      ParsePositiveInt(page_size, "page_size", kDefaultPageSize, kMinPageSize, kMaxPageSize);
  if (!parsed_size)
    return parsed_size.GetError();
  result.page_size = parsed_size.Value();

  if (!sort_by.empty()) {
    result.sort_by = ToLower(std::string(sort_by));
  }
  if (!sort_order.empty()) {
    const std::string order = ToLower(std::string(sort_order));
    if (order != "asc" && order != "desc") {
      return Error{"INVALID_ARGUMENT", "sort_order 只能是 asc 或 desc"};
    }
    result.sort_order = order;
  }
  return result;
}

Result<std::string> BuildOrderBy(
    const Pagination& pagination,
    std::initializer_list<std::pair<std::string_view, std::string_view>> whitelist) {
  for (const auto& entry : whitelist) {
    if (entry.first == pagination.sort_by) {
      // 排序片段来自白名单常量，不是用户输入
      return std::string(entry.second) + " " + (pagination.sort_order == "desc" ? "DESC" : "ASC");
    }
  }
  return Error{"INVALID_ARGUMENT", "不支持的排序字段: " + pagination.sort_by};
}

}  // namespace lan_file_hub
