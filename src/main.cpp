#include "common/Logger.h"
#include "common/Config.h"
#include "common/CmdArgs.h"
#include "pipeline/Pipeline.h"
#include "version.h"

#include <csignal>
#include <atomic>
#include <chrono>
#include <iostream>
#include <filesystem>
#include <thread>
#include <unistd.h>

// 全局停止标志（信号处理）
static std::atomic<bool> g_stop{false};

static void signalHandler(int sig) {
    LOG_WARN("Signal {} received, shutting down...", sig);
    g_stop = true;
}

int main(int argc, char* argv[]) {
    // 参数解析
    media_agent::CmdArgs args;
    if (!media_agent::parseArgs(argc, argv, args)) {
        std::cerr << "Failed to parse command line arguments\n";
        return EXIT_FAILURE;
    }
    const std::string& config_path = args.config_path;

    // 加载配置
    media_agent::AppConfig cfg;
    try {
        cfg = media_agent::AppConfig::loadFromFile(config_path);
    } catch (const std::exception& e) {
        std::cerr << "[CRITICAL] Failed to load config: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    // 日志初始化
    std::filesystem::create_directories(cfg.log.log_dir);
    media_agent::Logger::init(cfg.log.log_dir,
                              spdlog::level::from_str(cfg.log.level),
                              cfg.log.max_file_mb, cfg.log.max_files);

    LOG_INFO("media_agent starting. version={}, pid={}", VERSION_STRING, getpid());
    LOG_INFO("config={} agent_id={} socket_path={}",
             config_path, cfg.socket.agent_id, cfg.socket.socket_path);

    // 注册信号
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);   // 忽略 broken pipe（Socket 写失败）

    // 启动 Pipeline
    media_agent::Pipeline pipeline(std::move(cfg));
    if (!pipeline.start()) {
        LOG_CRITICAL("Pipeline start failed, exiting...");
        return EXIT_FAILURE;
    }

    // 主线程等待退出信号
    LOG_INFO("Running... press Ctrl+C to stop");
    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 优雅退出
    pipeline.stop();
    media_agent::Logger::flush();
    LOG_INFO("media_agent exited cleanly");
    return EXIT_SUCCESS;
}

