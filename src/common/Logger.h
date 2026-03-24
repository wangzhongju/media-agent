#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <memory>
#include <string>

namespace media_agent {

class Logger {
public:
    static void init(const std::string& log_dir = "log",
                     spdlog::level::level_enum level = spdlog::level::info,
                     size_t max_file_mb = 50,
                     size_t max_files   = 5)
    {
        try {
            std::vector<spdlog::sink_ptr> sinks;

            // 彩色终端输出
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(level);
            sinks.push_back(console_sink);

            // 滚动文件输出
            std::string log_path = log_dir + "/media_agent.log";
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                log_path, max_file_mb * 1024 * 1024, max_files);
            file_sink->set_level(level);
            sinks.push_back(file_sink);

            auto logger = std::make_shared<spdlog::logger>(
                "media_agent", sinks.begin(), sinks.end());
            logger->set_level(level);
            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
            logger->flush_on(spdlog::level::warn);

            spdlog::set_default_logger(logger);
            spdlog::info("Logger initialized. log_dir={}", log_dir);
        } catch (const spdlog::spdlog_ex& ex) {
            fprintf(stderr, "Logger init failed: %s\n", ex.what());
        }
    }

    static void setLevel(spdlog::level::level_enum level) {
        spdlog::set_level(level);
    }

    static void flush() {
        spdlog::default_logger()->flush();
    }
};

} // namespace media_agent

// 便捷宏
#define LOG_TRACE(...)    spdlog::trace(__VA_ARGS__)
#define LOG_DEBUG(...)    spdlog::debug(__VA_ARGS__)
#define LOG_INFO(...)     spdlog::info(__VA_ARGS__)
#define LOG_WARN(...)     spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...)    spdlog::error(__VA_ARGS__)
#define LOG_CRITICAL(...) spdlog::critical(__VA_ARGS__)

