// yolov8DetEngine.cpp
// Merges yolov8_det init/inference + postprocess into a single C++ class.
// Uses RKNN zero-copy (ZERO_COPY path) for input/output tensor memory.

#include "yolov8_det/yolov8DetEngine.h"
#include "yolov8_det/postprocess.h"
#include "image_utils.h"
#include "image_drawing.h"
#include "file_utils.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <iostream>
#include <set>
#include <algorithm>

// ---- class-based color palette (ARGB8888) ----
static const unsigned int CLASS_COLORS[] = {
    0xFFFF0000, // red
    0xFF00FF00, // green
    0xFF0000FF, // blue
    0xFFFFFF00, // yellow
    0xFFFF4500, // orange
    0xFFFF00FF, // magenta
    0xFF00FFFF, // cyan
    0xFF800080, // purple
    0xFF008080, // teal
    0xFFFFA500, // orange2
    0xFF00FF7F, // spring green
    0xFFDC143C, // crimson
    0xFF1E90FF, // dodger blue
    0xFF32CD32, // lime green
    0xFFFF69B4, // hot pink
    0xFF8B4513, // saddle brown
};
static const int NUM_CLASS_COLORS = sizeof(CLASS_COLORS) / sizeof(CLASS_COLORS[0]);

// ============================================================
//  constructor / destructor / resource management
// ============================================================
Yolov8DetEngine::Yolov8DetEngine() {}

Yolov8DetEngine::~Yolov8DetEngine() {
    releaseResources();
}

void Yolov8DetEngine::releaseResources() {
    if (!app_ctx_) return;

    // release zero-copy input memory
    for (int i = 0; i < (int)app_ctx_->io_num.n_input; ++i) {
        if (app_ctx_->input_mems[i]) {
            rknn_destroy_mem(app_ctx_->rknn_ctx, app_ctx_->input_mems[i]);
            app_ctx_->input_mems[i] = nullptr;
        }
    }
    // release zero-copy output memory
    for (int i = 0; i < (int)app_ctx_->io_num.n_output; ++i) {
        if (app_ctx_->output_mems[i]) {
            rknn_destroy_mem(app_ctx_->rknn_ctx, app_ctx_->output_mems[i]);
            app_ctx_->output_mems[i] = nullptr;
        }
    }

    // All attr arrays are allocated with malloc in init(), so free() here.
    if (app_ctx_->input_attrs)        { free(app_ctx_->input_attrs);        app_ctx_->input_attrs        = nullptr; }
    if (app_ctx_->output_attrs)       { free(app_ctx_->output_attrs);       app_ctx_->output_attrs       = nullptr; }
    if (app_ctx_->input_native_attrs) { free(app_ctx_->input_native_attrs); app_ctx_->input_native_attrs = nullptr; }
    if (app_ctx_->output_native_attrs){ free(app_ctx_->output_native_attrs);app_ctx_->output_native_attrs= nullptr; }

    if (app_ctx_->rknn_ctx) {
        rknn_destroy(app_ctx_->rknn_ctx);
        app_ctx_->rknn_ctx = 0;
    }
    delete app_ctx_;
    app_ctx_ = nullptr;
}

void Yolov8DetEngine::dump_tensor_attr(rknn_tensor_attr *attr) {
    char dims[128] = {0};
    for (int i = 0; i < attr->n_dims; ++i) {
        int idx = (int)strlen(dims);
        sprintf(&dims[idx], "%d%s", attr->dims[i], (i == attr->n_dims - 1) ? "" : ", ");
    }
    printf("  index=%d, name=%s, n_dims=%d, dims=[%s], n_elems=%d, size=%d, w_stride = %d, size_with_stride = %d, fmt=%s, type=%s, qnt_type=%s, zp=%d, scale=%f\n",
           attr->index,
           attr->name,
           attr->n_dims,
           dims,
           attr->n_elems,
           attr->size,
           attr->w_stride,
           attr->size_with_stride,
           get_format_string(attr->fmt),
           get_type_string(attr->type),
           get_qnt_type_string(attr->qnt_type),
           attr->zp,
           attr->scale);
}


int Yolov8DetEngine::init(const json &config) {
    // parse common fields (model_name_, model_path_, class_names_, thresholds…)
    if (parseConfig(config) != 0) return -1;

    if (model_path_.empty()) {
        std::cerr << "Yolov8DetEngine::init model_path is empty" << std::endl;
        return -1;
    }

    app_ctx_ = new rknn_app_context_t();
    std::memset(app_ctx_, 0, sizeof(rknn_app_context_t));

    int ret;
    int model_len = 0;
    char *model_data = nullptr;
    rknn_context ctx = 0;

    // Load RKNN Model
    model_len = read_data_from_file(model_path_.c_str(), &model_data);
    if (model_data == NULL || model_len <= 0) {
        std::cerr << "load_model fail! model_path=" << model_path_ << std::endl;
        releaseResources();
        return -1;
    }

    ret = rknn_init(&ctx, model_data, model_len, 0, NULL);
    free(model_data);
    if (ret < 0) {
        std::cerr << "rknn_init fail! ret=" << ret << std::endl;
        releaseResources();
        return -1;
    }

    // Get Model Input Output Number
    rknn_input_output_num io_num;
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret != RKNN_SUCC) {
        std::cerr << "rknn_query fail! ret=" << ret << std::endl;
        rknn_destroy(ctx);
        releaseResources();
        return -1;
    }
    printf("model input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);

    // Get Model Input Info (temporary stack-like vectors, copied to heap later)
    printf("input tensors:\n");
    std::vector<rknn_tensor_attr> input_native_attrs(io_num.n_input);
    memset(input_native_attrs.data(), 0, io_num.n_input * sizeof(rknn_tensor_attr));
    for (uint32_t i = 0; i < io_num.n_input; ++i) {
        input_native_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_NATIVE_INPUT_ATTR, &input_native_attrs[i], sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            std::cerr << "rknn_query fail! ret=" << ret << std::endl;
            rknn_destroy(ctx);
            releaseResources();
            return -1;
        }
        dump_tensor_attr(&input_native_attrs[i]);
    }

    // default input type is int8 (normalize and quantize need compute in outside)
    // if set uint8, will fuse normalize and quantize to npu
    input_native_attrs[0].type = RKNN_TENSOR_UINT8;
    app_ctx_->input_mems[0] = rknn_create_mem(ctx, input_native_attrs[0].size_with_stride);

    // Set input tensor memory
    ret = rknn_set_io_mem(ctx, app_ctx_->input_mems[0], &input_native_attrs[0]);
    if (ret < 0) {
        std::cerr << "input_mems rknn_set_io_mem fail! ret=" << ret << std::endl;
        rknn_destroy(ctx);
        releaseResources();
        return -1;
    }

    // Get Model Output Info
    printf("output tensors:\n");
    std::vector<rknn_tensor_attr> output_native_attrs(io_num.n_output);
    memset(output_native_attrs.data(), 0, io_num.n_output * sizeof(rknn_tensor_attr));
    for (uint32_t i = 0; i < io_num.n_output; ++i) {
        output_native_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_NATIVE_OUTPUT_ATTR, &output_native_attrs[i], sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            std::cerr << "rknn_query fail! ret=" << ret << std::endl;
            rknn_destroy(ctx);
            releaseResources();
            return -1;
        }
        dump_tensor_attr(&output_native_attrs[i]);
    }

    // Set output tensor memory
    for (uint32_t i = 0; i < io_num.n_output; ++i) {
        app_ctx_->output_mems[i] = rknn_create_mem(ctx, output_native_attrs[i].size_with_stride);
        ret = rknn_set_io_mem(ctx, app_ctx_->output_mems[i], &output_native_attrs[i]);
        if (ret < 0) {
            std::cerr << "output_mems rknn_set_io_mem fail! ret=" << ret << std::endl;
            rknn_destroy(ctx);
            releaseResources();
            return -1;
        }
    }

    // Set to context
    app_ctx_->rknn_ctx = ctx;

    // TODO
    if (output_native_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC &&
        output_native_attrs[0].type == RKNN_TENSOR_INT8) {
        app_ctx_->is_quant = true;
    } else {
        app_ctx_->is_quant = false;
    }

    // Query regular input attrs (used for postprocess dims)
    std::vector<rknn_tensor_attr> input_attrs(io_num.n_input);
    memset(input_attrs.data(), 0, io_num.n_input * sizeof(rknn_tensor_attr));
    for (uint32_t i = 0; i < io_num.n_input; ++i) {
        input_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &input_attrs[i], sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            std::cerr << "rknn_query fail! ret=" << ret << std::endl;
            releaseResources();
            return -1;
        }
    }

    // Query regular output attrs
    std::vector<rknn_tensor_attr> output_attrs(io_num.n_output);
    memset(output_attrs.data(), 0, io_num.n_output * sizeof(rknn_tensor_attr));
    for (uint32_t i = 0; i < io_num.n_output; ++i) {
        output_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &output_attrs[i], sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            std::cerr << "rknn_query fail! ret=" << ret << std::endl;
            releaseResources();
            return -1;
        }
    }

    // Persist all attr arrays to heap via malloc+memcpy (same as init_yolov8_model)
    app_ctx_->io_num = io_num;

    app_ctx_->input_attrs = (rknn_tensor_attr *)malloc(io_num.n_input * sizeof(rknn_tensor_attr));
    memcpy(app_ctx_->input_attrs, input_attrs.data(), io_num.n_input * sizeof(rknn_tensor_attr));

    app_ctx_->output_attrs = (rknn_tensor_attr *)malloc(io_num.n_output * sizeof(rknn_tensor_attr));
    memcpy(app_ctx_->output_attrs, output_attrs.data(), io_num.n_output * sizeof(rknn_tensor_attr));

    app_ctx_->input_native_attrs = (rknn_tensor_attr *)malloc(io_num.n_input * sizeof(rknn_tensor_attr));
    memcpy(app_ctx_->input_native_attrs, input_native_attrs.data(), io_num.n_input * sizeof(rknn_tensor_attr));

    app_ctx_->output_native_attrs = (rknn_tensor_attr *)malloc(io_num.n_output * sizeof(rknn_tensor_attr));
    memcpy(app_ctx_->output_native_attrs, output_native_attrs.data(), io_num.n_output * sizeof(rknn_tensor_attr));

    // Extract model spatial dimensions
    if (input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
        printf("model is NCHW input fmt\n");
        app_ctx_->model_channel = input_attrs[0].dims[1];
        app_ctx_->model_height  = input_attrs[0].dims[2];
        app_ctx_->model_width   = input_attrs[0].dims[3];
    } else {
        printf("model is NHWC input fmt\n");
        app_ctx_->model_height  = input_attrs[0].dims[1];
        app_ctx_->model_width   = input_attrs[0].dims[2];
        app_ctx_->model_channel = input_attrs[0].dims[3];
    }
    printf("model input height=%d, width=%d, channel=%d\n",
           app_ctx_->model_height, app_ctx_->model_width, app_ctx_->model_channel);

    return 0;
}

// ============================================================
//  infer
// ============================================================
int Yolov8DetEngine::infer(const image_buffer_t &image,
                           const filter_list_t &filters,
                           std::vector<object_result> &results) {
    return inferInternal(image, filters, results, nullptr);
}

int Yolov8DetEngine::infer(const image_buffer_t &image,
                           const filter_list_t &filters,
                           std::vector<object_result> &results,
                           image_buffer_t &drawed_image) {
    return inferInternal(image, filters, results, &drawed_image);
}

int Yolov8DetEngine::inferInternal(const image_buffer_t &image,
                           const filter_list_t &filters,
                           std::vector<object_result> &results,
                           image_buffer_t *drawed_image) {
    if (!app_ctx_) return -1;

    // Determine effective thresholds: prefer filter-level override if supplied
    float conf_thresh = (filters.confidence_threshold > 0.0f)
                            ? filters.confidence_threshold
                            : _conf_thresh;
    float iou_thresh  = _iou_thresh;

    image_buffer_t dst_img;
    letterbox_t    letter_box;
    memset(&dst_img,    0, sizeof(dst_img));
    memset(&letter_box, 0, sizeof(letter_box));

    // ---- pre-process: letterbox into zero-copy input buffer ----
    dst_img.width    = app_ctx_->model_width;
    dst_img.height   = app_ctx_->model_height;
    dst_img.format   = IMAGE_FORMAT_RGB888;
    dst_img.size     = get_image_size(&dst_img);
    dst_img.fd       = app_ctx_->input_mems[0]->fd;
    dst_img.virt_addr= (unsigned char *)app_ctx_->input_mems[0]->virt_addr;

    if (dst_img.virt_addr == NULL && dst_img.fd == 0) {
        std::cerr << "Yolov8DetEngine::infer input mem invalid!" << std::endl;
        return -1;
    }

    image_buffer_t src = image; // non-const copy required by RGA helper
    int ret = convert_image_with_letterbox(&src, &dst_img, &letter_box, 114);
    if (ret < 0) {
        std::cerr << "Yolov8DetEngine::infer letterbox failed ret=" << ret << std::endl;
        return -1;
    }

    // ---- run ----
    ret = rknn_run(app_ctx_->rknn_ctx, nullptr);
    if (ret < 0) {
        std::cerr << "rknn_run fail ret=" << ret << std::endl;
        return -1;
    }

    // ---- convert NC1HWC2 output to NCHW for postprocess ----
    const uint32_t n_output = app_ctx_->io_num.n_output;
    std::vector<rknn_output> outputs(n_output);
    memset(outputs.data(), 0, n_output * sizeof(rknn_output));
    for (uint32_t i = 0; i < n_output; ++i) {
        int channel = app_ctx_->output_attrs[i].dims[1];
        int h       = app_ctx_->output_attrs[i].n_dims > 2 ? app_ctx_->output_attrs[i].dims[2] : 1;
        int w       = app_ctx_->output_attrs[i].n_dims > 3 ? app_ctx_->output_attrs[i].dims[3] : 1;
        int zp      = app_ctx_->output_native_attrs[i].zp;
        float scale = app_ctx_->output_native_attrs[i].scale;

        if (app_ctx_->is_quant) {
            outputs[i].size = app_ctx_->output_native_attrs[i].n_elems * sizeof(int8_t);
            outputs[i].buf  = malloc(outputs[i].size);
            if (app_ctx_->output_native_attrs[i].fmt == RKNN_TENSOR_NC1HWC2) {
                NC1HWC2_i8_to_NCHW_i8(
                    (const int8_t *)app_ctx_->output_mems[i]->virt_addr,
                    (int8_t *)outputs[i].buf,
                    (int *)app_ctx_->output_native_attrs[i].dims,
                    channel, h, w, zp, scale);
            } else {
                memcpy(outputs[i].buf, app_ctx_->output_mems[i]->virt_addr, outputs[i].size);
            }
        } else {
            std::cerr << "Yolov8DetEngine: zero-copy does not support fp16 output" << std::endl;
            for (uint32_t j = 0; j < i; ++j) { if (outputs[j].buf) free(outputs[j].buf); }
            return -1;
        }
    }

    // ---- post-process ----
    ret = post_process(outputs.data(), &letter_box, conf_thresh, iou_thresh, results);

    for (uint32_t i = 0; i < n_output; ++i) {
        if (outputs[i].buf) free(outputs[i].buf);
    }
    if (ret != 0) return -1;

    if (drawed_image != nullptr) {
        // ---- draw detection boxes onto a copy of the original image AND normalize coordinates ----
        draw_and_normalize(image, results, *drawed_image);
    } else {
        normalize_results(image, results);
    }

    return 0;
}

void Yolov8DetEngine::normalize_results(const image_buffer_t &image, std::vector<object_result> &results) {
    for (auto &det : results) {
        float xcenter = det.box.x + det.box.w / 2.0f;
        float ycenter = det.box.y + det.box.h / 2.0f;
        det.box.x = xcenter / (float)image.width;
        det.box.y = ycenter / (float)image.height;
        det.box.w = det.box.w / (float)image.width;
        det.box.h = det.box.h / (float)image.height;
    }
}

void Yolov8DetEngine::draw_and_normalize(const image_buffer_t &image, std::vector<object_result> &results, image_buffer_t &drawed_image) {
    memset(&drawed_image, 0, sizeof(image_buffer_t));
    drawed_image.width  = image.width;
    drawed_image.height = image.height;
    drawed_image.format = image.format;
    drawed_image.size   = get_image_size(&drawed_image);
    drawed_image.virt_addr = (unsigned char *)malloc(drawed_image.size);
    if (drawed_image.virt_addr == nullptr) {
        std::cerr << "Yolov8DetEngine::infer failed to alloc drawed_image" << std::endl;
        return;
    }
    memcpy(drawed_image.virt_addr, image.virt_addr, drawed_image.size);

    const int thickness = 2;
    const int font_size = 12;
    
    // NOTE: For RGA rectangle drawing and text FreeType via CPU -> RGA layering:
    // The structure here demonstrates the logic separating CPU text drawing and RGA usage.
    // In a full implementation, you would use imfill/imrectangle for boxes, 
    // and draw FreeType text onto a clear canvas then imblend.
    // We currently use the CPU fallbacks (draw_rectangle / draw_text) to maintain compatibility.
    for (auto &det : results) {
        unsigned int color = CLASS_COLORS[det.class_id % NUM_CLASS_COLORS];
        
        // 1. Draw Rect (TODO: use librga imrectangle when fully available)
        draw_rectangle(&drawed_image, (int)det.box.x, (int)det.box.y, (int)det.box.w, (int)det.box.h, color, thickness);

        // 2. Draw text via CPU (e.g., FreeType)
        char label[128];
        if (!det.class_name.empty()) {
            snprintf(label, sizeof(label), "%s %.2f", det.class_name.c_str(), det.prop);
        } else {
            snprintf(label, sizeof(label), "%d %.2f", det.class_id, det.prop);
        }
        draw_text(&drawed_image, label, (int)det.box.x, (int)det.box.y > font_size ? (int)det.box.y - font_size : 0, color, font_size);

        // 3. Normalize coordinates: [left, top, w, h] to [xcenter, ycenter, w, h] normalized
    }

    normalize_results(image, results);
}

int Yolov8DetEngine::NC1HWC2_i8_to_NCHW_i8(const int8_t *src,
                                           int8_t *dst,
                                           int *dims,
                                           int channel,
                                           int h,
                                           int w,
                                           int zp,
                                           float scale) {
    (void)zp;    // reserved for future use
    (void)scale; // reserved for future use

    int batch  = dims[0];
    int C1     = dims[1];
    int C2     = dims[4];
    int hw_src = dims[2] * dims[3];
    int hw_dst = h * w;

    for (int i = 0; i < batch; i++) {
        const int8_t *src_b = src + i * C1 * hw_src * C2;
        int8_t *dst_b = dst + i * channel * hw_dst;
        for (int c = 0; c < channel; ++c) {
            int plane = c / C2;
            const int8_t *src_bc = plane * hw_src * C2 + src_b;
            int offset = c % C2;
            for (int cur_h = 0; cur_h < h; ++cur_h) {
                for (int cur_w = 0; cur_w < w; ++cur_w) {
                    int cur_hw = cur_h * w + cur_w;
                    dst_b[c * hw_dst + cur_hw] = src_bc[C2 * cur_hw + offset];
                }
            }
        }
    }

    return 0;
}

int Yolov8DetEngine::post_process(rknn_output *_outputs, letterbox_t *letter_box,
                                   float conf_threshold, float nms_threshold,
                                   std::vector<object_result> &od_results) {
    int class_num = class_names_.empty() ? 0 : static_cast<int>(class_names_.size());
    int ret = yolov8_det::post_process(app_ctx_, _outputs, letter_box, conf_threshold, nms_threshold, class_num, &od_results);
    if (ret != 0) {
        return ret;
    }

    for (auto &result : od_results) {
        if (result.class_id >= 0 && result.class_id < static_cast<int>(class_names_.size())) {
            result.class_name = class_names_[result.class_id];
        } else {
            result.class_name.clear();
        }
    }
    return 0;
}
