#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace media_agent {

struct SocketConfig {
    std::string socket_path = "/tmp/media_agent.sock";  // Unix Domain Socket
    int         send_queue_size  = 100;
    int         heartbeat_interval_s = 10;
    std::string agent_id;
};

struct LogConfig {
    std::string level    = "info";   // trace/debug/info/warn/error
    std::string log_dir  = "log";
    int         max_file_mb = 50;
    int         max_files   = 5;
};

struct PipelineConfig {
    int num_infer_threads = 3;    // 推理线程数，RK3588有3个NPU核
    int frame_queue_size  = 200;  // 帧队列容量（32 路拉流时适当加大）
    int statistics_interval_s = 5; // 统计日志输出周期（秒）
};

struct AppConfig {
    LogConfig                 log;
    SocketConfig              socket;
    PipelineConfig            pipeline;

    // 从 JSON 文件加载
    static AppConfig loadFromFile(const std::string& path);
    // 从 JSON 字符串加载
    static AppConfig loadFromString(const std::string& json_str);

    // 序列化为 JSON 字符串（调试用）
    std::string toJsonString() const;
};

} // namespace media_agent

