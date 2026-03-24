#pragma once

#include <array>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>

namespace media_agent {

inline std::string generateUuidV4() {
    thread_local std::mt19937_64 engine(std::random_device{}());
    std::uniform_int_distribution<uint32_t> dist(0, 255);

    std::array<uint8_t, 16> bytes{};
    for (auto& byte : bytes) {
        byte = static_cast<uint8_t>(dist(engine));
    }

    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0F) | 0x40);  // version 4
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3F) | 0x80);  // variant 10xx

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t index = 0; index < bytes.size(); ++index) {
        oss << std::setw(2) << static_cast<int>(bytes[index]);
        if (index == 3 || index == 5 || index == 7 || index == 9) {
            oss << '-';
        }
    }
    return oss.str();
}

}  // namespace media_agent