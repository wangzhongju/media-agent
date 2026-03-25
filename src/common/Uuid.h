#pragma once  // 防止头文件重复包含。

#include <array>    // 固定长度 16 字节数组。
#include <cstdint>  // uint8_t / uint32_t。
#include <iomanip>  // std::setw / std::setfill。
#include <random>   // 随机数引擎。
#include <sstream>  // std::ostringstream。
#include <string>   // std::string。

namespace media_agent {

// 生成一个 UUID v4 字符串。
// 这里不是依赖系统库，而是直接按标准拼装 16 个随机字节。
inline std::string generateUuidV4() {
    // 每个线程维护一套随机数引擎，减少多线程竞争。
    thread_local std::mt19937_64 engine(std::random_device{}());

    // 逐字节随机生成 0~255。
    std::uniform_int_distribution<uint32_t> dist(0, 255);

    // UUID 原始 16 字节。
    std::array<uint8_t, 16> bytes{};
    for (auto& byte : bytes) {
        byte = static_cast<uint8_t>(dist(engine));
    }

    // 设置 version = 4。
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0F) | 0x40);

    // 设置 variant = 10xx。
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3F) | 0x80);

    // 把字节格式化成标准 UUID 文本。
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

} // namespace media_agent