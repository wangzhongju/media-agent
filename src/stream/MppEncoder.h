#pragma once

#include "common/DmaImage.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include <rockchip/rk_mpi.h>
#include <rockchip/rk_venc_cfg.h>
}

namespace media_agent {

enum class MppEncoderType {
    H264,
    H265,
    JPEG,
};

/**
 * MPP 硬件视频编码器
 *
 * 职责：
 *   - 管理 MppCtx / MppApi / MppEncCfg 生命周期
 *   - 将外部 DMA 图像以 EXT_DMA 方式导入 MPP
 *   - 以零拷贝方式完成 NV12 / RGB → H.264 / H.265 / JPEG 编码
 *   - 异步取出编码后的 MppPacket，交给调用方处理
 *
 * 零拷贝路径：
 *   MppDecoder / EdgeInfer → DMA fd → mpp_buffer_import(EXT_DMA)
 *              → MppFrame → encode_put_frame → encode_get_packet
 *
 * 线程安全：单线程使用。
 */
class MppEncoder {
public:
    MppEncoder() = default;
    ~MppEncoder() { destroy(); }

    MppEncoder(const MppEncoder&)            = delete;
    MppEncoder& operator=(const MppEncoder&) = delete;

    /**
     * 初始化硬件编码器。
     *
     */
    bool init(MppEncoderType type,
              int width,
              int height,
              int hor_stride,
              int ver_stride,
              const std::string& stream_id,
              MppFrameFormat input_fmt,
              int fps = 25,
              int bitrate = 0);

    /** 销毁编码器并释放所有导入缓冲区。可安全重复调用。 */
    void destroy();

    /**
    * 将一块 DMA 图像送入 MPP 编码，并取出当前可用的所有输出包。
     *
    * @param image       输入图像，需在编码完成前保持有效
     * @param pts         输入帧时间戳
     * @param out_packets 输出：追加本次可获得的编码包
     *                    调用方负责在使用完后调用 mpp_packet_deinit()
     * @return true 送帧成功；编码队列满视为非致命
     */
    bool encodeImage(const std::shared_ptr<DmaImage>& image,
                 int64_t pts,
                 std::vector<MppPacket>& out_packets);

    /** 发送 EOS 并尽量取出剩余编码包。 */
    bool flush(std::vector<MppPacket>& out_packets);

    /** 请求下一帧强制编码为 IDR。仅 H.264 / H.265 有效。 */
    bool forceIdr();

    static MppCodingType toMppCoding(MppEncoderType type);
    static MppFrameFormat toMppFrameFormat(image_format_t format);
    static const char*   typeName(MppEncoderType type);

private:
    static int alignUp(int value, int alignment);
    bool configure();
    MppBuffer importExternalBuffer(const DmaImage& image);
    void drainPackets(std::vector<MppPacket>& out_packets);

    MppCtx                         ctx_        = nullptr;
    MppApi*                        mpi_        = nullptr;
    MppEncCfg                      cfg_        = nullptr;
    MppEncoderType                 type_       = MppEncoderType::H264;
    MppCodingType                  coding_     = MPP_VIDEO_CodingUnused;
    MppFrameFormat                 input_fmt_  = MPP_FMT_BGR888;
    int                            width_      = 0;
    int                            height_     = 0;
    int                            hor_stride_ = 0;
    int                            ver_stride_ = 0;
    int                            fps_        = 25;
    int                            bitrate_    = 0;
    std::string                    stream_id_;
    bool                           initialized_ = false;
    std::unordered_map<int, MppBuffer> imported_buffers_;
    std::deque<std::shared_ptr<DmaImage>> pending_inputs_;
};

} // namespace media_agent