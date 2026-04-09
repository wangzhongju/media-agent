# FindMPP.cmake
# Find Rockchip MPP (Media Process Platform) library
#
# Imported targets:
#   MPP::mpp
#
# Result variables:
#   MPP_FOUND
#   MPP_INCLUDE_DIRS
#   MPP_LIBRARIES
#
# Hints:
#   MPP_ROOT  — root directory of MPP installation

set(_MPP_THIRD_PARTY_ROOT "${CMAKE_SOURCE_DIR}/third_party/mpp")

# Some vendor packages only ship librockchip_mpp.so.0.
# Create compatibility symlinks so find_library can resolve rockchip_mpp.
if(EXISTS "${_MPP_THIRD_PARTY_ROOT}/lib/librockchip_mpp.so.0")
    if(NOT EXISTS "${_MPP_THIRD_PARTY_ROOT}/lib/librockchip_mpp.so.1")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E create_symlink librockchip_mpp.so.0 librockchip_mpp.so.1
            WORKING_DIRECTORY "${_MPP_THIRD_PARTY_ROOT}/lib"
        )
    endif()
    if(NOT EXISTS "${_MPP_THIRD_PARTY_ROOT}/lib/librockchip_mpp.so")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E create_symlink librockchip_mpp.so.1 librockchip_mpp.so
            WORKING_DIRECTORY "${_MPP_THIRD_PARTY_ROOT}/lib"
        )
    endif()
endif()

find_path(MPP_INCLUDE_DIR
    NAMES rockchip/rk_mpi.h
    HINTS
        ${_MPP_THIRD_PARTY_ROOT}/include
        ${MPP_ROOT}/include
)

find_path(MPP_INCLUDE_DIR
    NAMES rockchip/rk_mpi.h
    HINTS
        /usr/include
        /usr/local/include
)

find_library(MPP_LIBRARY
    NAMES rockchip_mpp
    HINTS
        ${_MPP_THIRD_PARTY_ROOT}/lib
        ${MPP_ROOT}/lib
)

find_library(MPP_LIBRARY
    NAMES rockchip_mpp
    HINTS
        /usr/lib
        /usr/local/lib
        /usr/lib/aarch64-linux-gnu
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MPP
    REQUIRED_VARS MPP_LIBRARY MPP_INCLUDE_DIR
)

if(MPP_FOUND AND NOT TARGET MPP::mpp)
    set(MPP_INCLUDE_DIRS ${MPP_INCLUDE_DIR})
    set(MPP_LIBRARIES    ${MPP_LIBRARY})

    add_library(MPP::mpp UNKNOWN IMPORTED)
    set_target_properties(MPP::mpp PROPERTIES
        IMPORTED_LOCATION         "${MPP_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MPP_INCLUDE_DIR}"
    )
    message(STATUS "Found MPP: ${MPP_LIBRARY} (include: ${MPP_INCLUDE_DIR})")
endif()

mark_as_advanced(MPP_INCLUDE_DIR MPP_LIBRARY)

