add_library(es_eventedge STATIC
    src/factory/event_detector_factory.cpp
    src/event/target_detection_detector.cpp
    src/event/person_leave_detector.cpp
    src/event/crowd_gather_detector.cpp
    src/event/person_running_detector.cpp
    src/event/vehicle_reverse_detector.cpp
    src/event/vehicle_parking_detector.cpp
)

target_include_directories(es_eventedge
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/event
        ${CMAKE_CURRENT_SOURCE_DIR}/include/factory
)

target_compile_features(es_eventedge PUBLIC cxx_std_17)
