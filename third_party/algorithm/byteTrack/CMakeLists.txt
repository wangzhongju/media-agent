cmake_minimum_required(VERSION 3.16)

#========================
# Project
#========================

set(CUR_LIB cdky_track)
project(${CUR_LIB})

include(cmake/modules.cmake)

add_subdirectory(bytetrack)

include(cmake/srcs.cmake)

set(${CUR_LIB} PARENT_SCOPE)
