cmake_minimum_required(VERSION 3.16)
project(media_agent VERSION 1.0.0 LANGUAGES CXX)

# ── C++ 标准 ──────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── 编译选项 ──────────────────────────────────────────────
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fPIC")

# ── 输出目录 ──────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ── 运行 DMA 环境权限检测───────────────────────────────────
include(${CMAKE_SOURCE_DIR}/cmake/CheckDmaEnv.cmake)

# ── 构建时将 config 目录复制到 bin/ ──────────────────────
file(COPY ${CMAKE_SOURCE_DIR}/config DESTINATION ${CMAKE_BINARY_DIR}/bin)

# ── 自定义 cmake 模块路径 ─────────────────────────────────
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# ── 第三方：FFmpeg（仅用于 RTSP demux，解码由 MPP 承担）──
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavformat
    libavcodec
    libavutil
    libswscale
)

# ── 第三方：Rockchip MPP（硬件视频解码）────────────────────
find_package(MPP REQUIRED)

set(EDGE_INFER_ROOT ${CMAKE_SOURCE_DIR}/third_party/edgeInfer)
set(EDGE_INFER_INCLUDE_DIR ${EDGE_INFER_ROOT}/include)
find_library(EDGE_INFER_LIBRARY
    NAMES edgeDeploy
    PATHS ${EDGE_INFER_ROOT}/lib
)
if(NOT EDGE_INFER_LIBRARY)
    find_library(EDGE_INFER_LIBRARY NAMES edgeDeploy)
endif()

if(NOT EDGE_INFER_LIBRARY)
    message(FATAL_ERROR "EdgeDeploy library NOT found: libedgeDeploy.so is required")
endif()
message(STATUS "EdgeDeploy found: ${EDGE_INFER_LIBRARY}")

# ── 第三方：Protobuf（结构化消息序列化）────────────────────
find_package(Protobuf REQUIRED)

# ── 第三方：算法库（可选，接口待定）──────────────────────
# 当 third_party/algo_lib 或 ALGO_LIB_ROOT 存在时自动启用
find_package(AlgoLib QUIET)
if(AlgoLib_FOUND)
    message(STATUS "AlgoLib found: ${ALGO_LIB_LIBRARY}")
    add_compile_definitions(HAVE_ALGO_LIB)
else()
    message(STATUS "AlgoLib NOT found, detector runs in stub mode")
endif()

# ── 第三方：nlohmann/json（header-only，源码在 third_party）─
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    ${CMAKE_SOURCE_DIR}/third_party/json/include
)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)

# ── 第三方：spdlog（header-only，源码在 third_party）────────
add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE
    ${CMAKE_SOURCE_DIR}/third_party/spdlog/include
)
# 不定义 SPDLOG_COMPILED_LIB → 自动使用 header-only 模式
add_library(spdlog::spdlog ALIAS spdlog)

# ── Proto 编译 ────────────────────────────────────────────
set(PROTO_SRC_DIR  ${CMAKE_SOURCE_DIR}/third_party/protocol/protobufs/media-agent)
# protobuf_generate_cpp 将文件输出到 CMAKE_CURRENT_BINARY_DIR（即 CMAKE_BINARY_DIR）

file(GLOB PROTO_FILES "${PROTO_SRC_DIR}/*.proto")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# 将生成的 proto 源文件封装为 OBJECT 库，确保 GENERATED 属性在正确的目录作用域生效
# 注意：protobuf_generate_cpp 将文件输出到 CMAKE_CURRENT_BINARY_DIR（即 CMAKE_BINARY_DIR），
#       而非 PROTO_GEN_DIR，因此 include 路径必须用 CMAKE_BINARY_DIR
add_library(proto_objects OBJECT ${PROTO_SRCS} ${PROTO_HDRS})
target_include_directories(proto_objects PUBLIC
    ${CMAKE_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)

# ── 子模块 ────────────────────────────────────────────────
add_subdirectory(src)

