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
# include(${CMAKE_SOURCE_DIR}/cmake/CheckDmaEnv.cmake)

# ── 构建时将 config 目录复制到 bin/ ──────────────────────
file(COPY ${CMAKE_SOURCE_DIR}/config DESTINATION ${CMAKE_BINARY_DIR}/bin)

# ── 自定义 cmake 模块路径 ─────────────────────────────────
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

set(TARGET_SOC "$ENV{TARGET_SOC}" CACHE STRING "Target SoC for edgeDeploy/rknn runtime")
if(NOT TARGET_SOC)
    set(TARGET_SOC "rk3588" CACHE STRING "Target SoC for edgeDeploy/rknn runtime" FORCE)
endif()
message(STATUS "TARGET_SOC: ${TARGET_SOC}")

# —— edgeDeploy ——
set(EDGE_DEPLOY_SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/edgeDeploy)
if(NOT EXISTS ${EDGE_DEPLOY_SOURCE_DIR}/CMakeLists.txt)
    message(FATAL_ERROR "EdgeDeploy submodule NOT found: ${EDGE_DEPLOY_SOURCE_DIR}")
endif()

add_subdirectory(${EDGE_DEPLOY_SOURCE_DIR} third_party/edgeDeploy)

target_include_directories(edgeDeploy PUBLIC
    ${EDGE_DEPLOY_SOURCE_DIR}/edgeDeploy/include
)

message(STATUS "EdgeDeploy added as subdirectory: ${EDGE_DEPLOY_SOURCE_DIR}")

# ── 跟踪 ────────────────────────────────────────────────
set(ALGORITHM_ROOT ${CMAKE_SOURCE_DIR}/third_party/algorithm)
set(BYTETRACK_SUBMODULE_DIR ${ALGORITHM_ROOT}/byteTrack)
set(EVENTEDGE_SUBMODULE_DIR ${ALGORITHM_ROOT}/eventEdge)

if(NOT EXISTS ${BYTETRACK_SUBMODULE_DIR}/CMakeLists.txt)
    message(FATAL_ERROR "ByteTrack submodule NOT found: ${BYTETRACK_SUBMODULE_DIR}")
endif()

add_subdirectory(${BYTETRACK_SUBMODULE_DIR} third_party/algorithm/byteTrack)

target_include_directories(media_agent_bytetrack PUBLIC
    ${BYTETRACK_SUBMODULE_DIR}/include
)

message(STATUS "ByteTrack added as submodule: ${BYTETRACK_SUBMODULE_DIR}")

if(NOT EXISTS ${EVENTEDGE_SUBMODULE_DIR}/CMakeLists.txt)
    message(FATAL_ERROR "EventEdge source NOT found: ${EVENTEDGE_SUBMODULE_DIR}")
endif()

add_subdirectory(${EVENTEDGE_SUBMODULE_DIR} third_party/algorithm/eventEdge)

target_include_directories(media_agent_eventedge PUBLIC
    ${EVENTEDGE_SUBMODULE_DIR}/include
)

message(STATUS "EventEdge added as submodule: ${EVENTEDGE_SUBMODULE_DIR}")

# ── 第三方：FFmpeg ──
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavformat
    libavcodec
    libavutil
    libswscale
)

# ── 第三方：Rockchip MPP（硬件视频解码）────────────────────
find_package(MPP REQUIRED)

# ── 第三方：Protobuf（结构化消息序列化）────────────────────
find_package(Protobuf REQUIRED)
find_package(yaml-cpp REQUIRED)

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

set_target_properties(media_agent edgeDeploy media_agent_bytetrack media_agent_eventedge es_bytetrack PROPERTIES
    INSTALL_RPATH "$ORIGIN:$ORIGIN/lib"
)

install(TARGETS edgeDeploy media_agent_bytetrack media_agent_eventedge es_bytetrack
    LIBRARY DESTINATION lib
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/third_party/mpp/lib/
    DESTINATION lib
    FILES_MATCHING PATTERN "*.so*"
)

# 顶层安装阶段剔除 edgeDeploy 的开发/示例产物，统一作用于本地安装、deb 和 docker 镜像安装。
install(CODE [[
    set(_install_root "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
    file(REMOVE
        "${_install_root}/edgeDeploy_hardhat_demo"
        "${_install_root}/edgeDeploy_hardhat_video_demo"
    )
    file(REMOVE_RECURSE
        "${_install_root}/include"
        "${_install_root}/images"
        "${_install_root}/video"
    )
]])