# FindAlgoLib.cmake
# 查找第三方算法动态库（edgeDeploy）
#
# 用法：
#   find_package(AlgoLib REQUIRED)
#
# 导出目标：
#   AlgoLib::algo
#
# 搜索路径优先级：
#   1. ALGO_LIB_ROOT 环境变量 / CMake 变量
#   2. 项目内 third_party/algo_lib
#   3. 系统默认路径

set(_ALGO_SEARCH_PATHS
    $ENV{ALGO_LIB_ROOT}
    ${ALGO_LIB_ROOT}
    ${CMAKE_SOURCE_DIR}/third_party/edgeInfer
    /usr/local
    /usr
)

# ── 头文件 ────────────────────────────────────────────────
find_path(ALGO_LIB_INCLUDE_DIR
    NAMES infer/edgeInfer.h
    PATHS ${_ALGO_SEARCH_PATHS}
    PATH_SUFFIXES include
    NO_DEFAULT_PATH
)

# ── 动态库 ────────────────────────────────────────────────
find_library(ALGO_LIB_LIBRARY
    NAMES edgeDeploy
    PATHS ${_ALGO_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
    NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(AlgoLib
    REQUIRED_VARS ALGO_LIB_LIBRARY ALGO_LIB_INCLUDE_DIR
)

if(AlgoLib_FOUND AND NOT TARGET AlgoLib::algo)
    add_library(AlgoLib::algo SHARED IMPORTED)
    set_target_properties(AlgoLib::algo PROPERTIES
        IMPORTED_LOCATION             "${ALGO_LIB_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ALGO_LIB_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(ALGO_LIB_INCLUDE_DIR ALGO_LIB_LIBRARY)

