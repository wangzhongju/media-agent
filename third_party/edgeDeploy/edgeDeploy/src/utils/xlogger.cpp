#include "xlogger.h"
#include "filesystem.hpp"
#include <stdexcept>

namespace fs = ghc::filesystem;

std::shared_ptr<spdlog::logger> XLogger::logger_ = nullptr;

void XLogger::init(const std::string& log_dir, spdlog::level::level_enum level) {
    if (logger_ != nullptr) return;

    try {
        fs::create_directories(log_dir);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to create log directory: " + std::string(e.what()));
    }

    std::string log_file = log_dir + "/app.log";

    // 1. 配置文件轮转
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        log_file, 10 * 1024 * 1024, 5, true
    );

    // 2. 配置控制台彩色输出
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

    // 3. 组合 sinks
    spdlog::sinks_init_list sinks = {console_sink, file_sink};

    // 4. 创建日志器
    logger_ = std::make_shared<spdlog::logger>("xlogger", sinks);
    
    // 5. 设置级别和格式
    logger_->set_level(level);
    // %s: 文件名 (basename)
    // %#: 行号
    // %!: 函数名 (可选)
    logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");

    // 6. 自动刷新设置：遇到 warn 级别立即刷新到磁盘，或者每 3 秒定时刷新
    logger_->flush_on(spdlog::level::warn);
    spdlog::flush_every(std::chrono::seconds(3));

    spdlog::set_default_logger(logger_);
}

std::shared_ptr<spdlog::logger>& XLogger::get_instance() {
    if (logger_ == nullptr) {
        init();
    }
    return logger_;
}

