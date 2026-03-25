#include "common/CmdArgs.h" // 命令行参数声明。

#include <getopt.h>   // getopt_long，用于解析长短参数。
#include <iostream>   // std::cout，用于打印帮助信息。
#include <cstdlib>    // EXIT_SUCCESS / EXIT_FAILURE 等常量所在头文件。

namespace media_agent {

namespace {

// 打印程序的使用说明。
// 这里不会退出程序，只负责把帮助文本输出到终端。
void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n"
              << "\n"
              << "Options:\n"
              << "  -c, --config <path>   Path to config file "
                 "(default: config/config.json)\n"
              << "  -h, --help            Show this help message\n";
}

} // namespace

// 解析命令行参数。
bool parseArgs(int argc, char* argv[], CmdArgs& args) {
    // 短参数定义:
    // `c:` 表示 `-c` 后面必须跟一个参数值。
    // `h`  表示 `-h` 不带参数。
    static const char* short_opts = "c:h";

    // 长参数定义表。
    static const struct option long_opts[] = {
        {"config", required_argument, nullptr, 'c'}, // --config <path>
        {"help", no_argument, nullptr, 'h'},         // --help
        {nullptr, 0, nullptr, 0},                     // 结束标记
    };

    // 保存 getopt_long 每次解析出的参数结果。
    int opt = 0;

    // 循环处理所有命令行选项，直到返回 -1 表示没有更多参数。
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'c':
                // 把用户传入的配置文件路径保存下来。
                args.config_path = optarg;
                break;
            case 'h':
                // 用户主动请求帮助，打印后返回 false，让上层直接退出。
                printUsage(argv[0]);
                return false;
            default:
                // 遇到非法参数时，getopt_long 通常已经输出了错误信息。
                // 这里再补一份完整帮助，方便用户修正命令。
                printUsage(argv[0]);
                return false;
        }
    }

    // 所有参数都合法，允许程序继续运行。
    return true;
}

} // namespace media_agent