#
# Try to find spdlog header-only library.
#
# This will define:
#   spdlog_FOUND
#   SPDLOG_FOUND
#   SPDLOG_INCLUDE_DIR
#
# Imported target:
#   spdlog::spdlog
#

set(_SPDLOG_SEARCH_PATHS
    $ENV{SPDLOG_ROOT}
    ${SPDLOG_ROOT}
    ${THIRD_PARTY_PATH}/spdlog
    ${CMAKE_CURRENT_LIST_DIR}/../3rdparty/spdlog
    /usr/local
    /usr
)

find_path(SPDLOG_INCLUDE_DIR
    NAMES spdlog/spdlog.h
    PATHS ${_SPDLOG_SEARCH_PATHS}
    PATH_SUFFIXES include
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(spdlog
    DEFAULT_MSG
    SPDLOG_INCLUDE_DIR
)

set(SPDLOG_FOUND ${spdlog_FOUND})

if(SPDLOG_FOUND)
    if(NOT TARGET spdlog AND NOT TARGET spdlog::spdlog)
        add_library(spdlog INTERFACE IMPORTED)
        set_target_properties(spdlog PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${SPDLOG_INCLUDE_DIR}"
        )
        add_library(spdlog::spdlog ALIAS spdlog)
    elseif(TARGET spdlog AND NOT TARGET spdlog::spdlog)
        add_library(spdlog::spdlog ALIAS spdlog)
    endif()
endif()

mark_as_advanced(SPDLOG_INCLUDE_DIR)
