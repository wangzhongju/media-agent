# FindRGA.cmake
# Find Rockchip RGA (Raster Graphic Acceleration) im2d library
#
# Imported targets:
#   RGA::rga
#
# Result variables:
#   RGA_FOUND
#   RGA_INCLUDE_DIRS
#   RGA_LIBRARIES
#
# Hints:
#   RGA_ROOT  — root directory of RGA installation

find_path(RGA_INCLUDE_DIR
    NAMES im2d.hpp im2d_type.h
    HINTS
        ${RGA_ROOT}/include
        /usr/include
        /usr/local/include
        /usr/include/rga
        /usr/local/include/rga
)

find_library(RGA_LIBRARY
    NAMES rga
    HINTS
        ${RGA_ROOT}/lib
        /usr/lib
        /usr/local/lib
        /usr/lib/aarch64-linux-gnu
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RGA
    REQUIRED_VARS RGA_LIBRARY RGA_INCLUDE_DIR
)

if(RGA_FOUND AND NOT TARGET RGA::rga)
    set(RGA_INCLUDE_DIRS ${RGA_INCLUDE_DIR})
    set(RGA_LIBRARIES    ${RGA_LIBRARY})

    add_library(RGA::rga UNKNOWN IMPORTED)
    set_target_properties(RGA::rga PROPERTIES
        IMPORTED_LOCATION         "${RGA_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${RGA_INCLUDE_DIR}"
    )
    message(STATUS "Found RGA: ${RGA_LIBRARY} (include: ${RGA_INCLUDE_DIR})")
endif()

mark_as_advanced(RGA_INCLUDE_DIR RGA_LIBRARY)

