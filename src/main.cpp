#include "common/Logger.h"     // 日志组件，负责统一输出运行日志。
#include "common/Config.h"     // 配置加载器，负责从 JSON 读取系统参数。
#include "common/CmdArgs.h"    // 命令行参数解析工具。
#include "pipeline/Pipeline.h" // 主业务流水线，负责拉流、推理、发布和 IPC。
#include "version.h"           // 程序版本号定义。

#include <csignal>      // std::signal，用来注册进程信号处理函数。
#include <atomic>       // std::atomic，用来实现线程安全的停止标志。
#include <chrono>       // 时间工具，用于 sleep_for。
#include <iostream>     // 标准错误输出，用于启动阶段打印关键错误。
#include <filesystem>   // 文件系统工具，用于创建日志目录。
#include <thread>       // std::this_thread::sleep_for。
#include <unistd.h>     // getpid，获取当前进程 ID。

// 这是一个全局的停止标志。
// 信号处理函数只做最简单的事情: 把它置为 true，通知主线程退出。
static std::atomic<bool> g_stop{false};

// 进程收到 SIGINT / SIGTERM 时会进入这里。
// 这里不做复杂清理，只记录日志并设置停止标志，避免在信号上下文里做危险操作。
static void signalHandler(int sig) {
    LOG_WARN("Signal {} received, shutting down...", sig);
    g_stop = true;
}

// 程序入口。
// 启动顺序大致是: 解析参数 -> 读取配置 -> 初始化日志 -> 注册信号 -> 启动 Pipeline -> 等待退出。
int main(int argc, char* argv[]) {
    // 创建命令行解析结果对象。
    media_agent::CmdArgs args;

    // 解析命令行参数。
    // 如果返回 false，通常表示用户传了 -h 查看帮助，或者传入了非法参数。
    if (!media_agent::parseArgs(argc, argv, args)) {
        std::cerr << "Failed to parse command line arguments\n";
        return EXIT_FAILURE;
    }

    // 取出配置文件路径，后面用于加载 JSON 配置。
    const std::string& config_path = args.config_path;

    // 准备一个配置对象，稍后从文件中填充。
    media_agent::AppConfig cfg;

    // 尝试读取并解析配置文件。
    try {
        cfg = media_agent::AppConfig::loadFromFile(config_path);
    } catch (const std::exception& e) {
        // 如果配置加载失败，此时日志系统还没初始化，只能直接输出到标准错误。
        std::cerr << "[CRITICAL] Failed to load config: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    // 先确保日志目录存在，避免文件日志初始化时因为目录不存在而失败。
    std::filesystem::create_directories(cfg.log.log_dir);

    // 初始化日志系统。
    // 这里会同时启用终端日志和滚动文件日志。
    media_agent::Logger::init(cfg.log.log_dir,
                              spdlog::level::from_str(cfg.log.level),
                              cfg.log.max_file_mb,
                              cfg.log.max_files);

    // 打印程序启动信息，方便排查版本和进程号。
    LOG_INFO("media_agent starting. version={}, pid={}", VERSION_STRING, getpid());

    // 打印当前最关键的配置项，便于确认连接的是哪个 agent 和 socket。
    LOG_INFO("config={} agent_id={} socket_path={}",
             config_path,
             cfg.socket.agent_id,
             cfg.socket.socket_path);

    // 注册 Ctrl+C 信号。
    std::signal(SIGINT, signalHandler);

    // 注册终止信号，例如 `kill <pid>` 默认发来的就是 SIGTERM。
    std::signal(SIGTERM, signalHandler);

    // 忽略 SIGPIPE。
    // 当 socket 对端已经关闭而本端继续写入时，系统可能产生这个信号。
    // 忽略后，我们改为通过 send/write 的返回值来处理错误。
    std::signal(SIGPIPE, SIG_IGN);

    // 创建主流水线对象。
    // 这里用 std::move 把配置交给 Pipeline 持有。
    media_agent::Pipeline pipeline(std::move(cfg));

    // 启动整个系统。
    // 如果启动失败，说明 IPC、线程或内部组件初始化出了问题。
    if (!pipeline.start()) {
        LOG_CRITICAL("Pipeline start failed, exiting...");
        return EXIT_FAILURE;
    }

    // 主线程本身不做业务处理，只负责等待退出信号。
    LOG_INFO("Running... press Ctrl+C to stop");
    while (!g_stop) {
        // 适当休眠，避免空转占满 CPU。
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 收到退出信号后，按顺序停止流水线。
    pipeline.stop();

    // 主动 flush 一次日志，尽量把最后的日志写盘。
    media_agent::Logger::flush();

    // 记录干净退出。
    LOG_INFO("media_agent exited cleanly");
    return EXIT_SUCCESS;
}