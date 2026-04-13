cmake_minimum_required(VERSION 3.16)

set(BYTETRACK_SOURCES
    utils.cpp
    dataType.cpp
    byte_track.cpp
    kalman_filter/kalmanfilter.cpp
    kalman_filter/linear_assignment.cpp
    kalman_filter/track.cpp
    munkres_assignment/hungarianoper.cpp
    munkres_assignment/munkres/munkres.cpp
)

find_package(Eigen3 QUIET)
if(EIGEN3_FOUND)
    set(BYTETRACK_EIGEN_INCLUDE_DIR ${EIGEN3_INCLUDE_DIR})
elseif(EXISTS "/usr/include/eigen3")
    set(BYTETRACK_EIGEN_INCLUDE_DIR /usr/include/eigen3)
elseif(EXISTS "/usr/local/include/eigen3")
    set(BYTETRACK_EIGEN_INCLUDE_DIR /usr/local/include/eigen3)
else()
    message(FATAL_ERROR "Eigen3 not found! Please install libeigen3-dev")
endif()

add_library(es_bytetrack STATIC ${BYTETRACK_SOURCES})
set_target_properties(es_bytetrack PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(es_bytetrack
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/kalman_filter
        ${CMAKE_CURRENT_SOURCE_DIR}/munkres_assignment
        ${CMAKE_CURRENT_SOURCE_DIR}/munkres_assignment/munkres
        ${BYTETRACK_EIGEN_INCLUDE_DIR}
)

target_link_libraries(es_bytetrack
    PUBLIC
        pthread
)

target_compile_features(es_bytetrack PUBLIC cxx_std_17)
