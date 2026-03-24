#pragma once

#include <string>

namespace media_agent {

/// 命令行解析结果
struct CmdArgs {
    std::string config_path = "config/config.json";  ///< -c / --config
};

/**
 * @brief  解析命令行参数（基于 getopt_long）
 *
 * 支持的选项：
 *   -c <path>  /  --config <path>   指定配置文件路径
 *   -h         /  --help            打印用法后返回 false
 *
 * @param argc   main() 的 argc
 * @param argv   main() 的 argv
 * @param args   [out] 解析结果
 * @return true  解析成功，可继续运行
 * @return false 遇到 --help 或非法选项，调用方应退出
 */
bool parseArgs(int argc, char* argv[], CmdArgs& args);

}  // namespace media_agent

