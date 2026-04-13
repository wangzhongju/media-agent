cmake_minimum_required(VERSION 3.16)

add_subdirectory(bytetrack)

add_library(media_agent_bytetrack SHARED
    src/MediaAgentTracker.cpp
)

target_include_directories(media_agent_bytetrack
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(media_agent_bytetrack
    PRIVATE
        es_bytetrack
        pthread
)

target_compile_features(media_agent_bytetrack PUBLIC cxx_std_17)

add_library(media_agent::bytetrack ALIAS media_agent_bytetrack)

if(COMMAND media_agent_register_algorithm_target)
    media_agent_register_algorithm_target(media_agent_bytetrack)
endif()
