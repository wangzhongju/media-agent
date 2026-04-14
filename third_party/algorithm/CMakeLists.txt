cmake_minimum_required(VERSION 3.16)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" AND CMAKE_SYSTEM_NAME MATCHES "Linux")
   message("Current device is Orin.")
   set (BUILD_ENV orin)
else ()
   message("Current device is x86.")
   set (BUILD_ENV x86)
endif()

set(SYSTEM_VERSION "")
find_program(LSB_RELEASE_EXEC lsb_release)
if(LSB_RELEASE_EXEC)
    execute_process(
        COMMAND ${LSB_RELEASE_EXEC} -rs
        OUTPUT_VARIABLE SYSTEM_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
elseif(EXISTS "/etc/os-release")
    file(STRINGS "/etc/os-release" OS_RELEASE_VERSION_LINE
        REGEX "^VERSION_ID="
    )
    if(OS_RELEASE_VERSION_LINE)
        string(REPLACE "VERSION_ID=" "" SYSTEM_VERSION "${OS_RELEASE_VERSION_LINE}")
        string(REPLACE "\"" "" SYSTEM_VERSION "${SYSTEM_VERSION}")
    endif()
endif()

if (SYSTEM_VERSION STREQUAL "22.04")
    set(VERSION 2204)
elseif (SYSTEM_VERSION STREQUAL "20.04")
    set(VERSION 2004)
else()
    message(WARNING "Unsupported ubuntu version '${SYSTEM_VERSION}', fallback to 20.04 defaults")
    set(VERSION 2004)
endif()

set(ALGORITHM_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND CMAKE_MODULE_PATH ${ALGORITHM_ROOT_DIR}/cmake)
list(APPEND CMAKE_PREFIX_PATH ${ALGORITHM_ROOT_DIR}/cmake)
set(THIRD_PARTY_PATH ${ALGORITHM_ROOT_DIR}/3rdparty)
set(THIRD_PARTY_PATH_ENV ${THIRD_PARTY_PATH}/${BUILD_ENV})

if (BUILD_TEST)
  include(cmake/gtest.cmake)
endif()

include(cmake/generate.cmake)
