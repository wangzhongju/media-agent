#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include "common/Config.h"
#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include <strings.h>
#include <cstdio>

namespace media_agent {

class Logger {
public:
    static void init(const LogConfig& cfg)
    {
        try {
            const auto level = spdlog::level::from_str(cfg.level);
            std::string output_mode = "file";
            if (strcasecmp(cfg.output.c_str(), "console") == 0) {
                output_mode = "console";
            }

            std::vector<spdlog::sink_ptr> sinks;

            if (output_mode == "console") {
                auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                console_sink->set_level(level);
                sinks.push_back(console_sink);
            } else {
                std::filesystem::create_directories(cfg.log_dir);
                std::string log_path = cfg.log_dir + "/media_agent.log";
                auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    log_path, static_cast<size_t>(cfg.max_file_mb) * 1024 * 1024, static_cast<size_t>(cfg.max_files));
                file_sink->set_level(level);
                sinks.push_back(file_sink);
            }

            auto logger = std::make_shared<spdlog::logger>(
                "media_agent", sinks.begin(), sinks.end());
            logger->set_level(level);
            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
            logger->flush_on(spdlog::level::warn);
            spdlog::flush_every(std::chrono::seconds(3));

            spdlog::set_default_logger(logger);
            if (output_mode == "console") {
                spdlog::info("Logger initialized. output=console");
            } else {
                spdlog::info("Logger initialized. output=file log_dir={}", cfg.log_dir);
            }
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

