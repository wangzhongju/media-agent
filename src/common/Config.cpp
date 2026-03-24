#include "Config.h"
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

namespace media_agent {

// ── 辅助：安全取值 ────────────────────────────────────────
template<typename T>
static T jget(const json& j, const std::string& key, T def) {
    if (j.contains(key) && !j[key].is_null())
        return j[key].get<T>();
    return def;
}

static SocketConfig parseSocket(const json& j) {
    SocketConfig c;
    c.socket_path           = jget<std::string>(j, "socket_path", "/tmp/media_agent.sock");
    c.send_queue_size       = jget<int>(j, "send_queue_size", 100);
    c.heartbeat_interval_s  = jget<int>(j, "heartbeat_interval_s", 10);
    c.agent_id              = jget<std::string>(j, "agent_id", "agent_001");
    return c;
}

static PipelineConfig parsePipeline(const json& j) {
    PipelineConfig c;
    c.num_infer_threads = jget<int>(j, "num_infer_threads", 3);
    c.frame_queue_size  = jget<int>(j, "frame_queue_size",  200);
    c.statistics_interval_s = jget<int>(j, "statistics_interval_s", 5);
    return c;
}

static LogConfig parseLog(const json& j) {
    LogConfig c;
    c.level       = jget<std::string>(j, "level", "info");
    c.log_dir     = jget<std::string>(j, "log_dir", "log");
    c.max_file_mb = jget<int>(j, "max_file_mb", 50);
    c.max_files   = jget<int>(j, "max_files", 5);
    return c;
}

static AppConfig parseJson(const json& root) {
    AppConfig cfg;
    if (root.contains("log"))      cfg.log      = parseLog(root["log"]);
    if (root.contains("socket"))   cfg.socket   = parseSocket(root["socket"]);
    if (root.contains("pipeline")) cfg.pipeline = parsePipeline(root["pipeline"]);
    return cfg;
}

AppConfig AppConfig::loadFromFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open())
        throw std::runtime_error("Cannot open config file: " + path);
    json root;
    ifs >> root;
    return parseJson(root);
}

AppConfig AppConfig::loadFromString(const std::string& json_str) {
    json root = json::parse(json_str);
    return parseJson(root);
}

std::string AppConfig::toJsonString() const {
    json j;
    j["log"]["level"]        = log.level;
    j["log"]["log_dir"]      = log.log_dir;
    j["socket"]["socket_path"]              = socket.socket_path;
    j["socket"]["heartbeat_interval_s"]     = socket.heartbeat_interval_s;
    j["pipeline"]["num_infer_threads"]      = pipeline.num_infer_threads;
    j["pipeline"]["frame_queue_size"]       = pipeline.frame_queue_size;
    j["pipeline"]["statistics_interval_s"]  = pipeline.statistics_interval_s;
    return j.dump(2);
}

} // namespace media_agent

