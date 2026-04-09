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

find_path(MPP_INCLUDE_DIR
    NAMES rockchip/rk_mpi.h
    HINTS
        ${MPP_ROOT}/include
        /usr/include
        /usr/local/include
)

find_library(MPP_LIBRARY
    NAMES rockchip_mpp
    HINTS
        ${MPP_ROOT}/lib
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

