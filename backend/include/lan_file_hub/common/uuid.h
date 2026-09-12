// UUID v4 生成。不引入第三方依赖：request_id 与后续对象 Key 都用得到。
#pragma once

#include <string>

namespace lan_file_hub {

// 返回标准小写 8-4-4-4-12 形式的 UUID v4。
[[nodiscard]] std::string GenerateUuidV4();

}  // namespace lan_file_hub
