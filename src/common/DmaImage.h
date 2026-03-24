#pragma once

#include "infer/ed_common.h"

#include <cstddef>
#include <memory>

namespace media_agent {

struct DmaImage {
    int            fd            = -1;
    void*          virt_addr     = nullptr;
    size_t         size          = 0;
    int            width         = 0;
    int            height        = 0;
    int            width_stride  = 0;
    int            height_stride = 0;
    image_format_t format        = IMAGE_FORMAT_YUV420SP_NV12;
    std::shared_ptr<void> owner;
};

} // namespace media_agent