#pragma once  // 防止这个版本头文件被重复包含。

// 主版本号。
// 一般表示不兼容的大改动。
#define VERSION_MAJOR 1

// 次版本号。
// 一般表示兼容性的功能增强。
#define VERSION_MINOR 0

// 修订号。
// 一般表示小修复或补丁发布。
#define VERSION_PATCH 0

// 把宏参数原样字符串化。
// 例如 STRINGIFY(12) 会得到 "12"。
#define STRINGIFY(x) #x

// 先展开宏，再做字符串化。
// 例如 TOSTRING(VERSION_MAJOR) 最终得到 "1" 而不是 "VERSION_MAJOR"。
#define TOSTRING(x) STRINGIFY(x)

// 生成可直接打印的语义化版本字符串，例如 "1.0.0"。
#define VERSION_STRING \
    TOSTRING(VERSION_MAJOR) "." \
    TOSTRING(VERSION_MINOR) "." \
    TOSTRING(VERSION_PATCH)

// 生成便于做整数比较的版本号。
// 例如 1.2.3 会编码成 10203。
#define VERSION_NUMBER \
    (VERSION_MAJOR * 10000 + \
     VERSION_MINOR * 100 + \
     VERSION_PATCH)