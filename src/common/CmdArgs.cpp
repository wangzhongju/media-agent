#include "common/CmdArgs.h"

#include <getopt.h>
#include <iostream>
#include <cstdlib>

namespace media_agent {

namespace {

void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n"
              << "\n"
              << "Options:\n"
              << "  -c, --config <path>   Path to config file "
                 "(default: config/config.json)\n"
              << "  -h, --help            Show this help message\n";
}

}  // namespace

bool parseArgs(int argc, char* argv[], CmdArgs& args) {
    static const char* short_opts = "c:h";
    static const struct option long_opts[] = {
        {"config", required_argument, nullptr, 'c'},
        {"help",   no_argument,       nullptr, 'h'},
        {nullptr,  0,                 nullptr,  0 },
    };

    int opt = 0;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'c':
                args.config_path = optarg;
                break;
            case 'h':
                printUsage(argv[0]);
                return false;
            default:
                // getopt_long 已打印 "invalid option" 信息
                printUsage(argv[0]);
                return false;
        }
    }
    return true;
}

}  // namespace media_agent

