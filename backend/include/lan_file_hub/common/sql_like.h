// SQL LIKE 模式的安全构造（2026-10-03，搜索功能）。
//
// 为什么单独放这里：搜索要把用户输入拼进 `ILIKE` 的**模式**里，而 `%` 与 `_` 在 LIKE 里是
// 通配符 —— 不转义的话，用户搜一个 `%` 就会命中所有文件（看起来像"搜索坏了"），
// 搜 `_` 会匹配任意单字符。两个仓储（文件与文件夹）都要用同一套规则，所以抽到 common。
//
// ⛔ 只负责模式本身；真正的查询仍然必须走参数绑定（pqxx::params），不要把值拼进 SQL 文本。
#pragma once

#include <string>
#include <string_view>

namespace lan_file_hub {

// 把 `\`、`%`、`_` 转义成字面量；配合 SQL 里的 `ESCAPE '\'` 使用。
[[nodiscard]] std::string EscapeLikePattern(std::string_view raw);

// 常用形态："包含 raw 这个子串" → `%<转义后的 raw>%`
[[nodiscard]] std::string LikeContains(std::string_view raw);

// 去掉首尾空白（搜索框里粘进来的空格很常见，别当成关键字的一部分）
[[nodiscard]] std::string TrimForSearch(std::string_view raw);

}  // namespace lan_file_hub
