#pragma once  // 防止头文件重复包含。

#include "infer/ed_common.h" // image_format_t 等图像格式定义。

#include <cstddef> // size_t。
#include <memory>  // std::shared_ptr，用于共享底层资源所有权。

namespace media_agent {

// 一张 DMA 图像的轻量描述。
// 它本身不负责真正分配内存，只是把一块 DMA 缓冲区的信息组织起来。
struct DmaImage {
    int fd = -1;                           // DMA-BUF 文件描述符，硬件模块之间通常靠它传递零拷贝缓冲。
    void* virt_addr = nullptr;             // CPU 可访问的虚拟地址，部分场景下可能为空。
    size_t size = 0;                       // 缓冲区总字节数。
    int width = 0;                         // 图像可见宽度。
    int height = 0;                        // 图像可见高度。
    int width_stride = 0;                  // 水平方向步长，通常大于等于 width。
    int height_stride = 0;                 // 垂直方向步长，通常大于等于 height。
    image_format_t format = IMAGE_FORMAT_YUV420SP_NV12; // 图像格式，默认 NV12。
    std::shared_ptr<void> owner;           // 底层资源所有者，确保图像用完前缓冲不会被提前释放。
};

} // namespace media_agent