cmake_minimum_required(VERSION 3.16)

#========================
# Project
#========================

set(CUR_LIB cdky_event)
project(${CUR_LIB})

include(cmake/modules.cmake)

add_subdirectory(event)

include(cmake/srcs.cmake)

set(${CUR_LIB} PARENT_SCOPE)
