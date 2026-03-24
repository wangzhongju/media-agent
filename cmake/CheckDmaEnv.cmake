# CheckDmaEnv.cmake
# 检查 /dev/dma_heap 权限

set(MEDIA_AGENT_UDEV_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/setup_udev_dma_heap.sh")
if(EXISTS /dev/dma_heap)
    execute_process(
        COMMAND bash -c "for n in /dev/dma_heap/system /dev/dma_heap/system-uncached /dev/dma_heap/cma; do if [[ -e \"$n\" && -r \"$n\" && -w \"$n\" ]]; then exit 0; fi; done; exit 1"
        RESULT_VARIABLE _dma_heap_perm_ret
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(NOT _dma_heap_perm_ret EQUAL 0)
        set(_suggest_user "$ENV{USER}")
        if("${_suggest_user}" STREQUAL "")
            set(_suggest_user "<your-user>")
        endif()
        message(FATAL_ERROR
            "No /dev/dma_heap permission. Run sudo setup_udev_dma_heap.sh, re-login, re-run cmake.")
    else()
        message(STATUS "dma_heap permission check passed for current user")
    endif()
else()
    message(FATAL_ERROR "/dev/dma_heap not found. Please check kernel/device config.")
endif()
