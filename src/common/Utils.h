#pragma once

#include <cctype>
#include <string>

namespace media_agent {

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

    return sanitized.empty() ? std::string("unknown") : sanitized;
}

}  // namespace media_agent
