#pragma once  // 防止头文件重复包含。

#include "common/DmaImage.h" // DmaImage。

#include <cstdint>       // int64_t。
#include <deque>         // pending_inputs_。
#include <memory>        // std::shared_ptr。
#include <string>        // std::string。
#include <unordered_map> // imported_buffers_。
#include <vector>        // 输出编码包。

extern "C" {
#include <rockchip/rk_mpi.h>      // MPP 核心接口。
#include <rockchip/rk_venc_cfg.h> // 编码器配置。
}

namespace media_agent {

enum class MppEncoderType {
    H264,
    H265,
    JPEG,
};

// MPP 硬件编码器封装。
// 它接收 DMA 图像输入，并输出 MppPacket 编码结果。
class MppEncoder {
public:
    MppEncoder() = default;
    ~MppEncoder() { destroy(); }

    MppEncoder(const MppEncoder&) = delete;
    MppEncoder& operator=(const MppEncoder&) = delete;

    bool init(MppEncoderType type,
              int width,
              int height,
              int hor_stride,
              int ver_stride,
              const std::string& stream_id,
              MppFrameFormat input_fmt,
              int fps = 25,
              int bitrate = 0);

    void destroy();

    bool encodeImage(const std::shared_ptr<DmaImage>& image,
                     int64_t pts,
                     std::vector<MppPacket>& out_packets);

    bool flush(std::vector<MppPacket>& out_packets);

    bool forceIdr();

    static MppCodingType toMppCoding(MppEncoderType type);
    static MppFrameFormat toMppFrameFormat(image_format_t format);
    static const char* typeName(MppEncoderType type);

private:
    static int alignUp(int value, int alignment);
    bool configure();
    MppBuffer importExternalBuffer(const DmaImage& image);
    void drainPackets(std::vector<MppPacket>& out_packets);

    MppCtx ctx_ = nullptr;
    MppApi* mpi_ = nullptr;
    MppEncCfg cfg_ = nullptr;
    MppEncoderType type_ = MppEncoderType::H264;
    MppCodingType coding_ = MPP_VIDEO_CodingUnused;
    MppFrameFormat input_fmt_ = MPP_FMT_BGR888;
    int width_ = 0;
    int height_ = 0;
    int hor_stride_ = 0;
    int ver_stride_ = 0;
    int fps_ = 25;
    int bitrate_ = 0;
    std::string stream_id_;
    bool initialized_ = false;
    std::unordered_map<int, MppBuffer> imported_buffers_;
    std::deque<std::shared_ptr<DmaImage>> pending_inputs_;
};

} // namespace media_agent