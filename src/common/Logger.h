#pragma once  // 防止头文件重复包含。

#include <spdlog/spdlog.h>                    // spdlog 核心接口。
#include <spdlog/sinks/stdout_color_sinks.h> // 彩色终端输出 sink。
#include <spdlog/sinks/rotating_file_sink.h> // 滚动文件 sink。
#include <memory>                            // std::shared_ptr。
#include <string>                            // std::string。

namespace media_agent {

// 日志封装类。
// 这里用非常轻量的静态方法包装 spdlog，避免业务代码到处写初始化细节。
class Logger {
public:
    // 初始化全局默认日志器。
    static void init(const std::string& log_dir = "log",
                     spdlog::level::level_enum level = spdlog::level::info,
                     size_t max_file_mb = 50,
                     size_t max_files = 5) {
        try {
            // sink 列表里可以同时放多个输出目的地。
            std::vector<spdlog::sink_ptr> sinks;

            // 创建控制台彩色输出 sink。
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(level);
            sinks.push_back(console_sink);

            // 创建滚动文件输出 sink。
            const std::string log_path = log_dir + "/media_agent.log";
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                log_path,
                max_file_mb * 1024 * 1024,
                max_files);
            file_sink->set_level(level);
            sinks.push_back(file_sink);

            // 创建 logger 实例，并把上面两个 sink 都挂上去。
            auto logger = std::make_shared<spdlog::logger>(
                "media_agent",
                sinks.begin(),
                sinks.end());

            // 设置全局日志级别。
            logger->set_level(level);

            // 设置日志格式。
            // 包括时间、等级、线程 ID 和正文。
            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");

            // warn 及以上等级立即 flush，减少关键日志丢失风险。
            logger->flush_on(spdlog::level::warn);

            // 设置成全局默认 logger，后面直接调用 spdlog::info 也会走这里。
            spdlog::set_default_logger(logger);

            // 打印初始化成功信息。
            spdlog::info("Logger initialized. log_dir={}", log_dir);
        } catch (const spdlog::spdlog_ex& ex) {
            // 如果日志系统自己初始化失败，只能回退到 stderr。
            fprintf(stderr, "Logger init failed: %s\n", ex.what());
        }
    }

    // 动态调整全局日志级别。
    static void setLevel(spdlog::level::level_enum level) {
        spdlog::set_level(level);
    }

    // 手动刷盘。
    static void flush() {
        spdlog::default_logger()->flush();
    }
};

} // namespace media_agent

// 下面这些宏只是为了让业务代码写起来更短。
#define LOG_TRACE(...) spdlog::trace(__VA_ARGS__)
#define LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define LOG_WARN(...) spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#define LOG_CRITICAL(...) spdlog::critical(__VA_ARGS__)