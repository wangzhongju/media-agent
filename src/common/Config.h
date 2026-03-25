#pragma once  // 防止头文件重复包含。

#include <string>            // 使用 std::string 保存文本配置。
#include <vector>            // 预留给数组型配置使用。
#include <nlohmann/json.hpp> // JSON 解析库。

namespace media_agent {

// Socket/IPC 相关配置。
struct SocketConfig {
    std::string socket_path = "/tmp/media_agent.sock";  // Unix Domain Socket 路径。
    int         send_queue_size  = 100;                          // 发送队列最大长度。
    int         heartbeat_interval_s = 10;                     // 心跳上报周期，单位秒。
    std::string agent_id;                              // 当前 agent 的唯一标识。
};

// 日志相关配置。
struct LogConfig {
    std::string level    = "info"; // 日志级别，可选 trace/debug/info/warn/error。
    std::string log_dir  = "log"; // 日志目录。
    int         max_file_mb = 50;         // 单个滚动日志文件的最大大小，单位 MB。
    int         max_files   = 5;            // 最多保留多少个滚动日志文件。
};

// 流水线相关配置。
struct PipelineConfig {
    int num_infer_threads = 3;     // 推理线程数，通常与可用 NPU/并发能力相关。
    int frame_queue_size = 200;    // 缓冲队列容量，流多时可以适当调大。
    int statistics_interval_s = 5; // 统计日志打印周期，单位秒。
};

// 程序总配置。
// 它把日志、IPC 和流水线参数统一收口，便于一次性传给主流程。
struct AppConfig {
    LogConfig log;            // 日志配置。
    SocketConfig socket;      // IPC 配置。
    PipelineConfig pipeline;  // 主流水线配置。

    // 从 JSON 文件读取配置。
    static AppConfig loadFromFile(const std::string& path);

    // 从 JSON 字符串读取配置。
    static AppConfig loadFromString(const std::string& json_str);

    // 把当前配置序列化回 JSON 字符串，便于调试打印。
    std::string toJsonString() const;
};

} // namespace media_agent