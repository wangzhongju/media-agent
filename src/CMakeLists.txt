# src/CMakeLists.txt

# ── 收集各子模块源文件 ─────────────────────────────────────
file(GLOB COMMON_SRCS CONFIGURE_DEPENDS   "common/*.cpp")
file(GLOB RTSP_SRCS CONFIGURE_DEPENDS     "stream/*.cpp")
file(GLOB DETECTOR_SRCS CONFIGURE_DEPENDS "detector/*.cpp")
file(GLOB IPC_SRCS CONFIGURE_DEPENDS      "ipc/*.cpp")
file(GLOB PIPELINE_SRCS CONFIGURE_DEPENDS "pipeline/*.cpp")
file(GLOB PROTOCOL_SRCS CONFIGURE_DEPENDS "protocol/*.cpp")
file(GLOB TRACKER_SRCS CONFIGURE_DEPENDS  "tracker/*.cpp")
file(GLOB EVENT_SRCS CONFIGURE_DEPENDS    "event/*.cpp")

list(FILTER RTSP_SRCS EXCLUDE REGEX ".*/RgaConverter\\.cpp$")

# ── 可执行目标 ─────────────────────────────────────────────
add_executable(media_agent
    main.cpp
    ${COMMON_SRCS}
    ${RTSP_SRCS}
    ${DETECTOR_SRCS}
    ${IPC_SRCS}
    ${PIPELINE_SRCS}
    ${PROTOCOL_SRCS}
    ${TRACKER_SRCS}
    ${EVENT_SRCS}
    $<TARGET_OBJECTS:proto_objects>   # 由根 CMakeLists 的 proto_objects OBJECT 库提供
)

# 显式声明编译阶段依赖，确保并行构建时 proto_objects（及其生成的 .pb.h）
# 在 media_agent 任何源文件开始编译之前已经完成
add_dependencies(media_agent proto_objects)

# ── 头文件搜索路径 ─────────────────────────────────────────
target_include_directories(media_agent PRIVATE
    ${CMAKE_SOURCE_DIR}/src       # 使模块间 include 路径统一（如 #include "common/Logger.h"）
    ${FFMPEG_INCLUDE_DIRS}        # libavformat / libavutil
    ${MPP_INCLUDE_DIRS}           # rockchip/rk_mpi.h 等
)
# media-agent.pb.h 路径（CMAKE_BINARY_DIR）和 Protobuf_INCLUDE_DIRS 由 proto_objects PUBLIC 自动传递

# ── 链接库 ────────────────────────────────────────────────
target_link_libraries(media_agent PRIVATE
    # Proto OBJECT 库（头文件路径 + 编译好的对象文件）
    proto_objects

    # FFmpeg（解码由 MPP 承担）
    ${FFMPEG_LIBRARIES}

    # Rockchip MPP（硬件视频解码）
    MPP::mpp

    # Protobuf
    ${Protobuf_LIBRARIES}

    # spdlog
    spdlog::spdlog

    # nlohmann/json（header-only，通过 INTERFACE 目标传递 include 路径）
    nlohmann_json::nlohmann_json

    # EdgeDeploy
    edgeDeploy

    # byteTrack
    cdky_track

    # event
    cdky_event

    # 系统库
    pthread
)

# ── 编译器警告（可选调整） ────────────────────────────────
target_compile_options(media_agent PRIVATE
    -Wall -Wextra -Wno-unused-parameter
)

# ── 安装规则 ──────────────────────────────────────────────
install(TARGETS media_agent RUNTIME DESTINATION .)
install(FILES   ${CMAKE_SOURCE_DIR}/config/config.json DESTINATION config)
