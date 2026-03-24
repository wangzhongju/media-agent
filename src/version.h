#pragma once

// ── 版本号组成 ────────────────────────────────────────────
#define VERSION_MAJOR  1
#define VERSION_MINOR  0
#define VERSION_PATCH  0

// ── 字符串格式 ────────────────────────────────────────────
// 辅助宏：把数字字面量转为字符串
#define STRINGIFY(x)      #x
#define TOSTRING(x)       STRINGIFY(x)

// "1.0.0"
#define VERSION_STRING \
    TOSTRING(VERSION_MAJOR) "." \
    TOSTRING(VERSION_MINOR) "." \
    TOSTRING(VERSION_PATCH)

// ── 数字格式（便于大小比较） ──────────────────────────────
// 编码规则：major*10000 + minor*100 + patch，例如 1.2.3 → 10203
#define VERSION_NUMBER \
    (VERSION_MAJOR * 10000 + \
     VERSION_MINOR * 100   + \
     VERSION_PATCH)
