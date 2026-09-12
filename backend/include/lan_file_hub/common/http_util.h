// HTTP 工具：Range 解析与 Content-Disposition 生成。
//
// 两件事都必须自己做，不能依赖框架默认行为：
//   - Range：要支持 bytes=start-end / start- / -suffix 三种写法，并正确返回 416
//   - Content-Disposition：文件名可能含中文与引号，RFC 6266 要求
//     filename*=UTF-8''<百分号编码>，直接拼进头里会产生非法响应头
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "lan_file_hub/common/result.h"

namespace lan_file_hub {

struct ByteRange {
  std::uint64_t start{0};
  std::uint64_t length{0};
};

// 解析 Range 头。
//   - 返回 RESOURCE_NOT_FOUND 以外的错误码表示请求不合法（应回 416）
//   - 不支持的写法（多段 range）按「忽略 Range，返回整个文件」处理：
//     此时返回的 range 覆盖整个文件
[[nodiscard]] Result<ByteRange> ParseByteRange(std::string_view range_header,
                                               std::uint64_t file_size);

// 生成安全的 Content-Disposition。
//   attachment; filename="fallback.bin"; filename*=UTF-8''%E4%B8%AD%E6%96%87.pdf
// 纯 ASCII 文件名时不输出 filename*，保持简洁。
[[nodiscard]] std::string BuildContentDisposition(std::string_view filename);

}  // namespace lan_file_hub
