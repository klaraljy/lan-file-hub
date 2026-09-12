// 分页与排序参数（docs/api.md §分页与排序）。
//
// 统一约束：1 <= page_size <= 100；sort_by 只能取白名单里的字段，
// 且**只允许映射到固定的 SQL 片段**，绝不把用户输入拼进 SQL。
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

#include "lan_file_hub/common/result.h"

namespace lan_file_hub {

inline constexpr int kMinPageSize = 1;
inline constexpr int kMaxPageSize = 100;
inline constexpr int kDefaultPageSize = 50;

struct Pagination {
  int page{1};
  int page_size{kDefaultPageSize};
  std::string sort_by{"name"};
  std::string sort_order{"asc"};

  [[nodiscard]] int Offset() const { return (page - 1) * page_size; }
};

// 解析并校验分页参数。
// sort_whitelist 里给出允许的 sort_by 取值；返回的 ordering_sql 是由调用方
// 提供的**固定 SQL 片段**（不是用户输入），用于直接拼接到 ORDER BY。
struct SortSpec {
  std::string field;
  std::string ordering_sql;  // 例如 "lower(f.name) ASC, f.id ASC"
};

[[nodiscard]] Result<Pagination> ParsePagination(std::string_view page, std::string_view page_size,
                                                 std::string_view sort_by,
                                                 std::string_view sort_order);

// 把 sort_by/sort_order 解析为固定 SQL；字段不在白名单时返回错误。
[[nodiscard]] Result<std::string> BuildOrderBy(
    const Pagination& pagination,
    std::initializer_list<std::pair<std::string_view, std::string_view>> whitelist);

}  // namespace lan_file_hub
