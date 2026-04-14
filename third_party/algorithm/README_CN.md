# Algorithm 模块

## 概述
`third_party/algorithm` 用于管理 `media-agent` 的独立算法包。
每个算法包对上层导出稳定的共享库目标，算法核心以内部静态库形式构建。

当前算法包：
- `byteTrack`：多目标跟踪模块。
- `eventEdge`：事件判定模块。

## 目录结构
```text
third_party/algorithm/
  CMakeLists.txt                # algorithm 根入口
  cmake/                        # 公共 CMake 工具与 Find 模块
    generate.cmake
    FindEigen3.cmake
    Findjson.cmake
    Findspdlog.cmake
    Findyaml-cpp.cmake
  3rdparty/                     # 可选内置三方依赖
  byteTrack/
    CMakeLists.txt
    cmake/
      modules.cmake
      srcs.cmake
    bytetrack/                  # ByteTrack 内核
    include/
    src/
  eventEdge/
    CMakeLists.txt
    cmake/
      modules.cmake
      srcs.cmake
    event/                      # EventEdge 内核
    include/
    src/
```

## 统一 CMake 规范
所有算法包遵循统一组织：
- `CMakeLists.txt`：模块入口，包含 `cmake/modules.cmake` 与 `cmake/srcs.cmake`。
- `cmake/modules.cmake`：声明并查找模块依赖（`find_package(...)`）。
- `cmake/srcs.cmake`：封装共享库源码收集与链接规则。

algorithm 根层行为：
- 上层工程只需要 `add_subdirectory(third_party/algorithm)`。
- 根 `CMakeLists.txt` 通过 `cmake/generate.cmake` 自动发现并加入算法包。
- `register_algorithm_target(...)` 统一安装目标和 RPATH。

## 导出目标
当前对上层导出的共享库目标：
- `cdky_track`（来自 `byteTrack`）
- `cdky_event`（来自 `eventEdge`）

内部静态库目标：
- `es_bytetrack`
- `es_eventedge`

## 公共 Find 模块
`third_party/algorithm/cmake` 提供算法包依赖查找：
- `FindEigen3.cmake`
- `Findjson.cmake`
- `Findspdlog.cmake`
- `Findyaml-cpp.cmake`

查找优先级统一为：
1. 显式环境变量/CMake 变量（`*_ROOT`）
2. `third_party/algorithm/3rdparty`
3. 系统默认路径

## 接入建议
- 上层仅链接共享库导出目标（`cdky_track`、`cdky_event`）。
- 不建议上层直接链接内部静态库。
- 算法封装层演进时需保持 C ABI 稳定。

## 维护说明
- 新增算法包时保持同样的 CMake 目录与文件结构。
- 新增三方依赖优先通过共享 `Find*.cmake` 接入。
- 行为、目标名或依赖变化时同步更新中英文 README。
