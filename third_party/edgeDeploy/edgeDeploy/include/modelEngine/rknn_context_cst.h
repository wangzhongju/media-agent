#pragma once

#include "rknn_api.h"

#if defined(RV1106_1103) 
    typedef struct {
        char *dma_buf_virt_addr;
        int dma_buf_fd;
        int size;
    }rknn_dma_buf;
#endif

typedef struct {
    rknn_context rknn_ctx;
    rknn_input_output_num io_num;
    rknn_tensor_attr* input_attrs;
    rknn_tensor_attr* output_attrs;
#if defined(RV1106_1103) 
    rknn_tensor_mem* input_mems[1];
    rknn_tensor_mem* output_mems[9];
    rknn_dma_buf img_dma_buf;
#endif
#if defined(ZERO_COPY)  
    rknn_tensor_mem* input_mems[1];
    rknn_tensor_mem* output_mems[9];
    rknn_tensor_attr* input_native_attrs;
    rknn_tensor_attr* output_native_attrs;
#endif
    int model_channel;
    int model_width;
    int model_height;
    bool is_quant;
} rknn_app_context_t;
