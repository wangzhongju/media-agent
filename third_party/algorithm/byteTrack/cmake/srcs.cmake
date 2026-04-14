#========================
# libs
#========================
set(CUR_SRCS "")
list(APPEND CUR_INCLUDES
    include
)

set(CUR_SUB_DIR "")
list(APPEND CUR_SUB_DIR
    include
    src
)

foreach (dir ${CUR_SUB_DIR})
    file(GLOB_RECURSE tmp_srcs ${dir}/*.cpp ${dir}/*.h)
    list(APPEND CUR_SRCS ${tmp_srcs})
endforeach ()

add_library(${CUR_LIB} SHARED
    ${CUR_SRCS}
)
target_include_directories(${CUR_LIB}
    PUBLIC
        ${CUR_INCLUDES}
    PRIVATE
        src
)

target_link_libraries(${CUR_LIB}
    PRIVATE
        es_bytetrack
        pthread
)

target_compile_features(${CUR_LIB} PUBLIC cxx_std_17)

if(COMMAND register_algorithm_target)
    register_algorithm_target(${CUR_LIB})
endif()
