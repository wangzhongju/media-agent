#
# Try to find yaml-cpp library.
#
# This will define:
#   yaml-cpp_FOUND
#   YAML_CPP_FOUND
#   YAML_CPP_INCLUDE_DIR
#   YAML_CPP_LIBRARY
#
# Imported targets:
#   yaml-cpp
#   yaml-cpp::yaml-cpp
#

set(_YAML_CPP_SEARCH_PATHS
    $ENV{YAML_CPP_ROOT}
    ${YAML_CPP_ROOT}
    ${THIRD_PARTY_PATH}/yaml-cpp
    ${CMAKE_CURRENT_LIST_DIR}/../3rdparty/yaml-cpp
    /usr/local
    /usr
)

find_path(YAML_CPP_INCLUDE_DIR
    NAMES yaml-cpp/yaml.h
    PATHS ${_YAML_CPP_SEARCH_PATHS}
    PATH_SUFFIXES include
)

find_library(YAML_CPP_LIBRARY
    NAMES yaml-cpp yaml-cppd
    PATHS ${_YAML_CPP_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args("yaml-cpp"
    DEFAULT_MSG
    YAML_CPP_INCLUDE_DIR
    YAML_CPP_LIBRARY
)

set(YAML_CPP_FOUND ${yaml-cpp_FOUND})
set(YAML_CPP_INCLUDE_DIRS ${YAML_CPP_INCLUDE_DIR})
set(YAML_CPP_LIBRARIES ${YAML_CPP_LIBRARY})

if(YAML_CPP_FOUND)
    if(NOT TARGET yaml-cpp)
        add_library(yaml-cpp UNKNOWN IMPORTED GLOBAL)
        set_target_properties(yaml-cpp PROPERTIES
            IMPORTED_LOCATION "${YAML_CPP_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${YAML_CPP_INCLUDE_DIR}"
        )
    endif()

    if(TARGET yaml-cpp AND NOT TARGET yaml-cpp::yaml-cpp)
        add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
    endif()
endif()

mark_as_advanced(YAML_CPP_INCLUDE_DIR YAML_CPP_LIBRARY)
