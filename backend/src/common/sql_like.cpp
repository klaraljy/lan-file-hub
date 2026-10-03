#include "lan_file_hub/common/sql_like.h"

namespace lan_file_hub {

std::string EscapeLikePattern(std::string_view raw) {
  std::string escaped;
  escaped.reserve(raw.size() + 8);
  for (const char ch : raw) {
    if (ch == '\\' || ch == '%' || ch == '_') {
      escaped.push_back('\\');
    }
    escaped.push_back(ch);
  }
  return escaped;
}

std::string LikeContains(std::string_view raw) {
  std::string pattern;
  pattern.reserve(raw.size() + 10);
  pattern.push_back('%');
  pattern += EscapeLikePattern(raw);
  pattern.push_back('%');
  return pattern;
}

std::string TrimForSearch(std::string_view raw) {
  std::size_t begin = 0;
  std::size_t end = raw.size();
  const auto is_space = [](char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
  };
  while (begin < end && is_space(raw[begin])) {
    ++begin;
  }
  while (end > begin && is_space(raw[end - 1])) {
    --end;
  }
  return std::string(raw.substr(begin, end - begin));
}

}  // namespace lan_file_hub
