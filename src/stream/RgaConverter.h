#pragma once  // 防止头文件重复包含。

#include <memory>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <string>
#include <cstddef>

extern "C" {
#include <rockchip/rk_mpi.h>
}

namespace media_agent {

// 一个由 dma_heap 分配得到的 DMA 缓冲区。
struct DmaBuffer {
    int fd = -1;          // DMA-BUF 文件描述符。
    void* vaddr = nullptr; // CPU 映射地址，可为空。
    size_t size = 0;      // 缓冲区字节数。

    DmaBuffer() = default;
    ~DmaBuffer();

    DmaBuffer(const DmaBuffer&) = delete;
    DmaBuffer& operator=(const DmaBuffer&) = delete;
    DmaBuffer(DmaBuffer&&) = delete;
    DmaBuffer& operator=(DmaBuffer&&) = delete;
};

// 简单的 DMA 缓冲池。
class DmaBufPool {
public:
    bool init(size_t buf_size, int count);
    std::shared_ptr<DmaBuffer> acquire(int timeout_ms = 100);

private:
    std::vector<std::unique_ptr<DmaBuffer>> all_bufs_;
    std::vector<DmaBuffer*> free_list_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

// RGA 颜色转换器。
// 它把 MPP 解码输出的 NV12 帧转换并缩放为目标尺寸的 BGR 图像。
class RgaConverter {
public:
    RgaConverter() = default;

    RgaConverter(const RgaConverter&) = delete;
    RgaConverter& operator=(const RgaConverter&) = delete;

    bool init(int dst_w, int dst_h, int pool_size, const std::string& stream_id);

    std::shared_ptr<DmaBuffer> convert(MppFrame mpp_frame,
                                       float* out_scale = nullptr,
                                       int* out_pad_x = nullptr,
                                       int* out_pad_y = nullptr);

private:
    DmaBufPool pool_;
    int dst_w_ = 640;
    int dst_h_ = 640;
    std::string stream_id_;
};

} // namespace media_agent