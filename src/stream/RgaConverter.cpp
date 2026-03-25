#include "RgaConverter.h"
#include "common/Logger.h"
#include <algorithm>
#include <chrono>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/dma-heap.h>

#include <im2d.hpp>
#include "RgaUtils.h"

namespace media_agent {

// 从 dma_heap 分配一个指定大小的 DMA-BUF。
static int dmaBufAlloc(size_t size) {
    static const char* kHeaps[] = {
        "/dev/dma_heap/system-uncached",
        "/dev/dma_heap/cma",
        "/dev/dma_heap/system",
    };
    for (const char* heap_path : kHeaps) {
        int heap_fd = open(heap_path, O_RDONLY | O_CLOEXEC);
        if (heap_fd < 0) {
            continue;
        }

        struct dma_heap_allocation_data alloc_data = {};
        alloc_data.len = static_cast<__u64>(size);
        alloc_data.fd_flags = O_RDWR | O_CLOEXEC;
        alloc_data.heap_flags = 0;

        int ret = ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc_data);
        close(heap_fd);
        if (ret == 0) {
            return static_cast<int>(alloc_data.fd);
        }
    }
    return -1;
}

DmaBuffer::~DmaBuffer() {
    if (vaddr && vaddr != MAP_FAILED) {
        munmap(vaddr, size);
        vaddr = nullptr;
    }
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

bool DmaBufPool::init(size_t buf_size, int count) {
    for (int i = 0; i < count; ++i) {
        auto buf = std::make_unique<DmaBuffer>();
        buf->size = buf_size;
        buf->fd = dmaBufAlloc(buf_size);
        if (buf->fd < 0) {
            LOG_ERROR("[DmaBufPool] dmaBufAlloc failed at buf #{} (size={})", i, buf_size);
            return false;
        }
        buf->vaddr = mmap(nullptr, buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, buf->fd, 0);
        if (buf->vaddr == MAP_FAILED) {
            buf->vaddr = nullptr;
        }
        free_list_.push_back(buf.get());
        all_bufs_.push_back(std::move(buf));
    }
    return true;
}

std::shared_ptr<DmaBuffer> DmaBufPool::acquire(int timeout_ms) {
    std::unique_lock<std::mutex> lk(mutex_);
    bool ok = cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                           [this] { return !free_list_.empty(); });
    if (!ok) {
        return nullptr;
    }

    DmaBuffer* raw = free_list_.back();
    free_list_.pop_back();

    return std::shared_ptr<DmaBuffer>(raw, [this](DmaBuffer* p) {
        std::lock_guard<std::mutex> guard(mutex_);
        free_list_.push_back(p);
        cv_.notify_one();
    });
}

bool RgaConverter::init(int dst_w, int dst_h, int pool_size, const std::string& stream_id) {
    dst_w_ = dst_w;
    dst_h_ = dst_h;
    stream_id_ = stream_id;

    const size_t buf_size = static_cast<size_t>(dst_w) * dst_h * get_bpp_from_format(RK_FORMAT_RGBA_8888);
    if (!pool_.init(buf_size, pool_size)) {
        LOG_ERROR("[RgaConverter] stream={} DmaBufPool init failed ({}x{} x {})",
                  stream_id_,
                  dst_w_,
                  dst_h_,
                  pool_size);
        return false;
    }

    LOG_INFO("[RgaConverter] stream={} ready dst={}x{} BGR24, pool={} bufs",
             stream_id_,
             dst_w_,
             dst_h_,
             pool_size);
    return true;
}

std::shared_ptr<DmaBuffer> RgaConverter::convert(MppFrame mpp_frame,
                                                 float* out_scale,
                                                 int* out_pad_x,
                                                 int* out_pad_y) {
    MppBuffer mpp_buf = mpp_frame_get_buffer(mpp_frame);
    if (!mpp_buf) {
        LOG_WARN("[RgaConverter] stream={} null mpp buffer", stream_id_);
        return nullptr;
    }

    int h_stride = static_cast<int>(mpp_frame_get_hor_stride(mpp_frame));
    int v_stride = static_cast<int>(mpp_frame_get_ver_stride(mpp_frame));
    int w = static_cast<int>(mpp_frame_get_width(mpp_frame));
    int h = static_cast<int>(mpp_frame_get_height(mpp_frame));

    int nv12_fd = mpp_buffer_get_fd(mpp_buf);
    int nv12_size = h_stride * v_stride * get_bpp_from_format(RK_FORMAT_YCbCr_420_SP);

    auto dst_dma = pool_.acquire(50);
    if (!dst_dma) {
        LOG_WARN("[RgaConverter] stream={} DmaBufPool acquire timeout, drop frame",
                 stream_id_);
        return nullptr;
    }

    float scale_w = static_cast<float>(dst_w_) / w;
    float scale_h = static_cast<float>(dst_h_) / h;
    float scale = std::min(scale_w, scale_h);

    int new_w = static_cast<int>(w * scale);
    int new_h = static_cast<int>(h * scale);
    new_w = new_w & ~1;
    new_h = new_h & ~1;

    int pad_x = (dst_w_ - new_w) / 2;
    int pad_y = (dst_h_ - new_h) / 2;
    pad_x = pad_x & ~1;
    pad_y = pad_y & ~1;

    if (out_scale) {
        *out_scale = scale;
    }
    if (out_pad_x) {
        *out_pad_x = pad_x;
    }
    if (out_pad_y) {
        *out_pad_y = pad_y;
    }

    rga_buffer_handle_t src_handle = importbuffer_fd(nv12_fd, nv12_size);
    rga_buffer_handle_t dst_handle = importbuffer_fd(dst_dma->fd, static_cast<int>(dst_dma->size));
    if (src_handle == 0 || dst_handle == 0) {
        LOG_WARN("[RgaConverter] stream={} importbuffer_fd failed (src={} dst={})",
                 stream_id_,
                 src_handle,
                 dst_handle);
        if (src_handle) {
            releasebuffer_handle(src_handle);
        }
        if (dst_handle) {
            releasebuffer_handle(dst_handle);
        }
        return nullptr;
    }

    rga_buffer_t src_buf = wrapbuffer_handle(src_handle,
                                             w,
                                             h,
                                             RK_FORMAT_YCbCr_420_SP,
                                             h_stride,
                                             v_stride);
    rga_buffer_t dst_buf = wrapbuffer_handle(dst_handle,
                                             dst_w_,
                                             dst_h_,
                                             RK_FORMAT_BGR_888);

    im_rect src_rect = {0, 0, w, h};
    im_rect dst_rect = {pad_x, pad_y, new_w, new_h};

    IM_STATUS ret = imcheck(src_buf, dst_buf, src_rect, dst_rect);
    if (ret != IM_STATUS_NOERROR) {
        LOG_WARN("[RgaConverter] stream={} imcheck failed status={}",
                 stream_id_,
                 static_cast<int>(ret));
        releasebuffer_handle(src_handle);
        releasebuffer_handle(dst_handle);
        return nullptr;
    }

    IM_STATUS ims = improcess(src_buf, dst_buf, {}, src_rect, dst_rect, {}, IM_SYNC);

    releasebuffer_handle(src_handle);
    releasebuffer_handle(dst_handle);

    if (ims != IM_STATUS_SUCCESS) {
        LOG_WARN("[RgaConverter] stream={} improcess(letterbox) failed status={}",
                 stream_id_,
                 static_cast<int>(ims));
        return nullptr;
    }

    return dst_dma;
}

} // namespace media_agent
