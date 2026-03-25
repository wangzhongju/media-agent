#pragma once  // 防止头文件重复包含。

#include <string> // 使用 std::string 保存配置文件路径。

namespace media_agent {

// 命令行解析结果。
// 当前程序只支持配置文件路径这一个核心参数。
struct CmdArgs {
    std::string config_path = "config/config.json"; // `-c` / `--config` 对应的配置文件路径。
};

// 解析命令行参数。
// 返回 true 表示参数合法并且程序可以继续运行。
// 返回 false 表示用户请求显示帮助，或者传入了非法参数。
bool parseArgs(int argc, char* argv[], CmdArgs& args);

} // namespace media_agent