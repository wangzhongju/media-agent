#include "Config.h"   // 配置结构与声明。
#include <fstream>     // std::ifstream，用于读取配置文件。
#include <stdexcept>   // std::runtime_error，用于抛出配置错误。

// 给 nlohmann::json 起一个短别名，后面写起来更简洁。
using json = nlohmann::json;

namespace media_agent {

// 安全读取 JSON 字段。
// 如果字段不存在或者字段值为 null，就返回默认值。
template<typename T>
static T jget(const json& j, const std::string& key, T def) {
    if (j.contains(key) && !j[key].is_null()) {
        return j[key].get<T>();
    }
    return def;
}

// 解析 socket 配置段。
static SocketConfig parseSocket(const json& j) {
    SocketConfig c;
    c.socket_path = jget<std::string>(j, "socket_path", "/tmp/media_agent.sock");
    c.send_queue_size = jget<int>(j, "send_queue_size", 100);
    c.heartbeat_interval_s = jget<int>(j, "heartbeat_interval_s", 10);
    c.agent_id = jget<std::string>(j, "agent_id", "agent_001");
    return c;
}

// 解析流水线配置段。
static PipelineConfig parsePipeline(const json& j) {
    PipelineConfig c;
    c.num_infer_threads = jget<int>(j, "num_infer_threads", 3);
    c.frame_queue_size = jget<int>(j, "frame_queue_size", 200);
    c.statistics_interval_s = jget<int>(j, "statistics_interval_s", 5);
    return c;
}

// 解析日志配置段。
static LogConfig parseLog(const json& j) {
    LogConfig c;
    c.level = jget<std::string>(j, "level", "info");
    c.log_dir = jget<std::string>(j, "log_dir", "log");
    c.max_file_mb = jget<int>(j, "max_file_mb", 50);
    c.max_files = jget<int>(j, "max_files", 5);
    return c;
}

// 解析根 JSON 对象。
// 这里允许某些段缺失，缺失时就保留结构体里的默认值。
static AppConfig parseJson(const json& root) {
    AppConfig cfg;
    if (root.contains("log")) {
        cfg.log = parseLog(root["log"]);
    }
    if (root.contains("socket")) {
        cfg.socket = parseSocket(root["socket"]);
    }
    if (root.contains("pipeline")) {
        cfg.pipeline = parsePipeline(root["pipeline"]);
    }
    return cfg;
}

// 从文件加载配置。
AppConfig AppConfig::loadFromFile(const std::string& path) {
    // 打开配置文件。
    std::ifstream ifs(path);

    // 如果打不开，直接抛异常交给上层处理。
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open config file: " + path);
    }

    // 读取整个 JSON 对象。
    json root;
    ifs >> root;

    // 解析并返回应用配置。
    return parseJson(root);
}

// 从内存里的 JSON 字符串加载配置。
AppConfig AppConfig::loadFromString(const std::string& json_str) {
    json root = json::parse(json_str);
    return parseJson(root);
}

// 把配置对象转成易读的 JSON 字符串。
std::string AppConfig::toJsonString() const {
    json j;

    // 序列化日志配置。
    j["log"]["level"] = log.level;
    j["log"]["log_dir"] = log.log_dir;
    j["log"]["max_file_mb"] = log.max_file_mb;
    j["log"]["max_files"] = log.max_files;

    // 序列化 socket 配置。
    j["socket"]["socket_path"] = socket.socket_path;
    j["socket"]["send_queue_size"] = socket.send_queue_size;
    j["socket"]["heartbeat_interval_s"] = socket.heartbeat_interval_s;
    j["socket"]["agent_id"] = socket.agent_id;

    // 序列化流水线配置。
    j["pipeline"]["num_infer_threads"] = pipeline.num_infer_threads;
    j["pipeline"]["frame_queue_size"] = pipeline.frame_queue_size;
    j["pipeline"]["statistics_interval_s"] = pipeline.statistics_interval_s;

    // 使用 2 个空格缩进，便于人眼阅读。
    return j.dump(2);
}

} // namespace media_agent