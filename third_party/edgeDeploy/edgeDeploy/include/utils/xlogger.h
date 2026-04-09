#ifndef XLOGGER_H
#define XLOGGER_H

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <memory>
#include <string>

class XLogger {
public:
    static void init(const std::string& log_dir = "./logs", 
                     spdlog::level::level_enum level = spdlog::level::info);

    static std::shared_ptr<spdlog::logger>& get_instance();

    XLogger(const XLogger&) = delete;
    XLogger& operator=(const XLogger&) = delete;

private:
    XLogger() = default;
    ~XLogger() { spdlog::shutdown(); }

    static std::shared_ptr<spdlog::logger> logger_;
};

// --- 优化后的日志宏定义 ---
// 使用 spdlog 自带的宏，这些宏会自动填充 source_loc (文件名和行号)
#define XLOG_TRACE(...)     SPDLOG_LOGGER_CALL(XLogger::get_instance(), spdlog::level::trace, __VA_ARGS__)
#define XLOG_DEBUG(...)     SPDLOG_LOGGER_CALL(XLogger::get_instance(), spdlog::level::debug, __VA_ARGS__)
#define XLOG_INFO(...)      SPDLOG_LOGGER_CALL(XLogger::get_instance(), spdlog::level::info, __VA_ARGS__)
#define XLOG_WARN(...)      SPDLOG_LOGGER_CALL(XLogger::get_instance(), spdlog::level::warn, __VA_ARGS__)
#define XLOG_ERROR(...)     SPDLOG_LOGGER_CALL(XLogger::get_instance(), spdlog::level::err, __VA_ARGS__)
#define XLOG_CRITICAL(...)  SPDLOG_LOGGER_CALL(XLogger::get_instance(), spdlog::level::critical, __VA_ARGS__)

#endif // XLOGGER_H
