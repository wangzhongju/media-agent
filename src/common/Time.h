#pragma once  // 防止头文件重复包含。

#include <chrono> // steady_clock / system_clock。
#include <ctime>  // std::time_t / std::strftime。
#include <string> // std::string。

namespace media_agent {

// 获取单调时钟的当前毫秒值。
// 适合做耗时统计和超时判断，不受系统时间回拨影响。
inline int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// 获取系统时钟的当前毫秒值。
// 适合做日志时间戳和对外时间语义。
inline int64_t systemNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// 生成形如 `20260324_153045` 的时间戳字符串。
// 常用于生成录制文件名等。
inline std::string makeTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&now_time, &tm_buf);

    char buf[32] = {0};
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm_buf);
    return std::string(buf);
}

} // namespace media_agent