#include "lan_file_hub/common/http_util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace lan_file_hub {
namespace {

constexpr std::string_view kPrefix = "bytes=";

bool IsAsciiPrintable(unsigned char c) {
  return c >= 0x20 && c < 0x7F;
}

// 百分号编码（RFC 3986 unreserved 之外全部编码），用于 filename*
std::string PercentEncode(std::string_view text) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size() * 3);
  for (const char raw : text) {
    const auto c = static_cast<unsigned char>(raw);
    const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
    if (unreserved) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0x0F]);
    }
  }
  return out;
}

// ASCII 回退名：去掉引号/反斜杠/控制字符，非 ASCII 换成 '_'
std::string AsciiFallback(std::string_view filename) {
  std::string out;
  out.reserve(filename.size());
  for (const char raw : filename) {
    const auto c = static_cast<unsigned char>(raw);
    if (c > 0x7F || !IsAsciiPrintable(c) || c == '"' || c == '\\' || c == '/' || c == ';') {
      out.push_back('_');
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
  if (out.empty()) {
    out = "download.bin";
  }
  return out;
}

bool IsAllDigitsOrEmpty(std::string_view text) {
  return std::all_of(text.begin(), text.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; });
}

Result<std::uint64_t> ToUint(std::string_view text) {
  if (!IsAllDigitsOrEmpty(text)) {
    return Error{"INVALID_ARGUMENT", "Range 不是合法数字"};
  }
  std::uint64_t value = 0;
  for (const char c : text) {
    const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
    if (value > (UINT64_MAX - digit) / 10ULL) {
      return Error{"INVALID_ARGUMENT", "Range 数值溢出"};
    }
    value = value * 10ULL + digit;
  }
  return value;
}

}  // namespace

Result<ByteRange> ParseByteRange(std::string_view range_header, std::uint64_t file_size) {
  ByteRange whole{0, file_size};

  if (range_header.empty() || file_size == 0) {
    return whole;
  }
  if (range_header.rfind(kPrefix, 0) != 0) {
    // 单位不是 bytes（例如 items=）：按规范忽略 Range
    return whole;
  }

  const std::string_view spec = range_header.substr(kPrefix.size());
  const auto dash = spec.find('-');
  if (dash == std::string_view::npos) {
    return Error{"INVALID_ARGUMENT", "Range 缺少 '-'"};
  }
  // 多段 range（含逗号）不支持：忽略，返回整个文件
  if (spec.find(',') != std::string_view::npos) {
    return whole;
  }

  const std::string_view first = spec.substr(0, dash);
  const std::string_view last = spec.substr(dash + 1);

  if (first.empty()) {
    // bytes=-N：最后 N 个字节
    if (last.empty()) {
      return Error{"INVALID_ARGUMENT", "Range 两端都为空"};
    }
    auto suffix = ToUint(last);
    if (!suffix)
      return suffix.GetError();
    if (suffix.Value() == 0) {
      return Error{"INVALID_ARGUMENT", "Range 长度为 0"};
    }
    const std::uint64_t length = std::min<std::uint64_t>(suffix.Value(), file_size);
    return ByteRange{file_size - length, length};
  }

  auto start = ToUint(first);
  if (!start)
    return start.GetError();
  if (start.Value() >= file_size) {
    // 起点越界：416
    return Error{"INVALID_ARGUMENT", "Range 起点超出文件大小"};
  }

  if (last.empty()) {
    return ByteRange{start.Value(), file_size - start.Value()};
  }

  auto end = ToUint(last);
  if (!end)
    return end.GetError();
  if (end.Value() < start.Value()) {
    return Error{"INVALID_ARGUMENT", "Range 结束位置小于起点"};
  }
  const std::uint64_t clamped_end = std::min<std::uint64_t>(end.Value(), file_size - 1);
  return ByteRange{start.Value(), clamped_end - start.Value() + 1};
}

std::string BuildContentDisposition(std::string_view filename) {
  const bool ascii_only = std::all_of(filename.begin(), filename.end(), [](unsigned char c) {
    return IsAsciiPrintable(c) && c != '"' && c != '\\';
  });

  const std::string fallback = AsciiFallback(filename);
  std::string header = "attachment; filename=\"" + fallback + "\"";
  if (!ascii_only) {
    header += "; filename*=UTF-8''" + PercentEncode(filename);
  }
  return header;
}

}  // namespace lan_file_hub
