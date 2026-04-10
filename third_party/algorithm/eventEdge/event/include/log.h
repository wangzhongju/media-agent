#pragma once

#include <stdio.h>

#define app_error(fmt, ...) fprintf(stderr, "[eventEdge][ERROR] " fmt, ##__VA_ARGS__)
#define app_warn(fmt, ...)  fprintf(stderr, "[eventEdge][WARN] " fmt, ##__VA_ARGS__)
#define app_info(fmt, ...)  fprintf(stdout, "[eventEdge][INFO] " fmt, ##__VA_ARGS__)
#define app_debug(fmt, ...) fprintf(stdout, "[eventEdge][DEBUG] " fmt, ##__VA_ARGS__)

