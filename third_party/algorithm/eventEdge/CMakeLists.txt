cmake_minimum_required(VERSION 3.16)

find_package(yaml-cpp REQUIRED)

add_subdirectory(event)

add_library(media_agent_eventedge SHARED
    src/MediaAgentEvent.cpp
)

target_include_directories(media_agent_eventedge
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/event/include
        ${CMAKE_CURRENT_SOURCE_DIR}/event/include/event
        ${CMAKE_CURRENT_SOURCE_DIR}/event/include/factory
)

set(EVENTEDGE_YAML_TARGET yaml-cpp)
if(TARGET yaml-cpp::yaml-cpp)
    set(EVENTEDGE_YAML_TARGET yaml-cpp::yaml-cpp)
endif()

target_link_libraries(media_agent_eventedge
    PRIVATE
        es_eventedge
        ${EVENTEDGE_YAML_TARGET}
        pthread
)

target_compile_features(media_agent_eventedge PUBLIC cxx_std_17)

add_library(media_agent::eventedge ALIAS media_agent_eventedge)

