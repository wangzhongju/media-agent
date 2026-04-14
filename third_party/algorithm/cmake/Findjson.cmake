#
# Try to find nlohmann/json header-only library.
#
# This will define:
#   json_FOUND
#   JSON_FOUND
#   JSON_INCLUDE_DIR
#
# Imported target:
#   nlohmann_json::nlohmann_json
#

set(_JSON_SEARCH_PATHS
    $ENV{JSON_ROOT}
    ${JSON_ROOT}
    $ENV{NLOHMANN_JSON_ROOT}
    ${NLOHMANN_JSON_ROOT}
    ${THIRD_PARTY_PATH}/json
    ${CMAKE_CURRENT_LIST_DIR}/../3rdparty/json
    /usr/local
    /usr
)

find_path(JSON_INCLUDE_DIR
    NAMES nlohmann/json.hpp
    PATHS ${_JSON_SEARCH_PATHS}
    PATH_SUFFIXES include
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(json
    DEFAULT_MSG
    JSON_INCLUDE_DIR
)

set(JSON_FOUND ${json_FOUND})

if(JSON_FOUND)
    if(NOT TARGET nlohmann_json AND NOT TARGET nlohmann_json::nlohmann_json)
        add_library(nlohmann_json INTERFACE IMPORTED)
        set_target_properties(nlohmann_json PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${JSON_INCLUDE_DIR}"
        )
        add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
    elseif(TARGET nlohmann_json AND NOT TARGET nlohmann_json::nlohmann_json)
        add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
    endif()
endif()

mark_as_advanced(JSON_INCLUDE_DIR)
