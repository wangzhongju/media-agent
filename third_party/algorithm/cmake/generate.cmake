function(register_algorithm_target target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Algorithm target not found: ${target_name}")
    endif()
    set_target_properties(${target_name} PROPERTIES
        INSTALL_RPATH "$ORIGIN:$ORIGIN/lib"
    )
    set_property(GLOBAL APPEND PROPERTY MEDIA_AGENT_ALGORITHM_INSTALL_TARGETS ${target_name})
endfunction()

file(GLOB ALGORITHM_MODULE_CMAKES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} */CMakeLists.txt)
list(SORT ALGORITHM_MODULE_CMAKES)

if(NOT ALGORITHM_MODULE_CMAKES)
    message(FATAL_ERROR "No algorithm modules found under: ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

foreach(module_cmake IN LISTS ALGORITHM_MODULE_CMAKES)
    get_filename_component(module_dir ${module_cmake} DIRECTORY)
    add_subdirectory(${module_dir})
    message(STATUS "Algorithm module added: ${module_dir}")
endforeach()

get_property(algorithm_install_targets GLOBAL PROPERTY MEDIA_AGENT_ALGORITHM_INSTALL_TARGETS)
if(algorithm_install_targets)
    list(REMOVE_DUPLICATES algorithm_install_targets)
    install(TARGETS ${algorithm_install_targets}
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION lib
    )
    message(STATUS "Algorithm install targets: ${algorithm_install_targets}")
endif()