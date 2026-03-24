#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <string>
#include <cstddef>

// Rockchip MPP（MppFrame 类型）
extern "C" {
#include <rockchip/rk_mpi.h>
}

namespace media_agent {

/**
 * DMA-buf 缓冲区句柄
 *
 * 封装一个从 /dev/dma_heap 分配的物理连续缓冲区。
 * 析构时自动 munmap（若已映射）+ close(fd)。
 * 不可拷贝/移动：通过 shared_ptr 共享所有权。
 */
struct DmaBuffer {
    int    fd    = -1;       // DMA-buf 文件描述符（传给 RGA/NPU）
    void*  vaddr = nullptr;  // CPU 映射地址（可选，调试用）
    size_t size  = 0;        // 缓冲区字节数

    DmaBuffer() = default;
    ~DmaBuffer();            // munmap + close(fd)

    DmaBuffer(const DmaBuffer&)            = delete;
    DmaBuffer& operator=(const DmaBuffer&) = delete;
    DmaBuffer(DmaBuffer&&)                 = delete;
    DmaBuffer& operator=(DmaBuffer&&)      = delete;
};

/**
 * DMA-buf 缓冲池（预分配，避免每帧 alloc/free 的系统调用开销）
 *
 * 线程安全：acquire() 带超时阻塞，shared_ptr 的自定义 deleter 负责归还。
 */
class DmaBufPool {
public:
    /**
     * 初始化缓冲池，分配 count 个大小为 buf_size 字节的 DMA-buf。
     * @return true 全部分配成功
     */
    bool init(size_t buf_size, int count);

    /**
     * 取出一个空闲缓冲区（阻塞等待，带超时）。
     * 返回的 shared_ptr 在最后一个引用释放时自动归还到池中。
     * @param timeout_ms 等待超时毫秒（默认 100ms）
     * @return 缓冲区，超时或池未就绪返回 nullptr
     */
    std::shared_ptr<DmaBuffer> acquire(int timeout_ms = 100);

private:
    std::vector<std::unique_ptr<DmaBuffer>> all_bufs_;  // 池内所有缓冲区
    std::vector<DmaBuffer*>                 free_list_; // 当前空闲
    std::mutex                              mutex_;
    std::condition_variable                 cv_;
};

/**
 * RGA 色彩转换 + 缩放器
 *
 * 职责：
 *   - 管理 RGA 输出 DMA-buf 缓冲池
 *   - 将 MPP 输出的 NV12 MppFrame 通过 RGA 硬件转换为 BGR24 并缩放到目标尺寸
 *
 * 零拷贝路径：
 *   mpp_buffer_get_fd() → importbuffer_fd × 2 → imresize → releasebuffer_handle × 2
 *   全程无 CPU 拷贝，RGA 和 NPU 均直接访问物理内存。
 */
class RgaConverter {
public:
    RgaConverter() = default;

    RgaConverter(const RgaConverter&)            = delete;
    RgaConverter& operator=(const RgaConverter&) = delete;

    /**
     * 初始化 RGA 转换器，预分配 DMA-buf 缓冲池。
     *
     * @param dst_w      目标宽度（像素）
     * @param dst_h      目标高度（像素）
     * @param pool_size  缓冲池大小（帧数）
     * @param stream_id  日志标识
     * @return true 初始化成功
     */
    bool init(int dst_w, int dst_h, int pool_size, const std::string& stream_id);

    /**
     * 将一帧 NV12 MppFrame 通过 RGA 硬件转换并缩放为 BGR24。
     *
     * 保持宽高比（letterbox）：按短边缩放，长边不足的部分上下/左右补黑边。
     *
     * @param mpp_frame  MPP 已解码帧（调用方持有所有权，此函数不调用 deinit）
     * @param out_scale  [out] 可选，实际缩放比例（用于坐标反映射）
     * @param out_pad_x  [out] 可选，左侧黑边像素数
     * @param out_pad_y  [out] 可选，顶部黑边像素数
     * @return BGR24 DMA-buf（shared_ptr 析构时自动归还池）；失败返回 nullptr
     */
    std::shared_ptr<DmaBuffer> convert(MppFrame mpp_frame,
                                       float* out_scale = nullptr,
                                       int*   out_pad_x = nullptr,
                                       int*   out_pad_y = nullptr);

private:
    DmaBufPool  pool_;
    int         dst_w_     = 640;
    int         dst_h_     = 640;
    std::string stream_id_;
};

} // namespace media_agent

