#pragma once  // 防止头文件重复包含。

#include <cctype> // std::isalnum。
#include <string> // std::string。

namespace media_agent {

// 清洗一段字符串，使它适合作为文件名中的单个片段。
// 规则很简单:
// 1. 字母、数字、横杠、下划线原样保留。
// 2. 其他字符统一替换成 `_`。
inline std::string sanitizeFileComponent(const std::string& value) {
    std::string sanitized;
    sanitized.reserve(value.size());

    for (char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_') {
            sanitized.push_back(static_cast<char>(ch));
        } else {
            sanitized.push_back('_');
        }
    }

    // 如果清洗后为空，就给一个兜底名字。
    return sanitized.empty() ? std::string("unknown") : sanitized;
}

} // namespace media_agent