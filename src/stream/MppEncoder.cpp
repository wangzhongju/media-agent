#include "MppEncoder.h"

#include "common/Logger.h"

extern "C" {
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/rk_mpi_cmd.h>
#include <rockchip/rk_venc_cmd.h>
#include <rockchip/rk_venc_rc.h>
}

namespace media_agent {

namespace {

constexpr int kDefaultFps = 25;

} // namespace

int MppEncoder::alignUp(int value, int alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

MppCodingType MppEncoder::toMppCoding(MppEncoderType type) {
    switch (type) {
        case MppEncoderType::H264: return MPP_VIDEO_CodingAVC;
        case MppEncoderType::H265: return MPP_VIDEO_CodingHEVC;
        case MppEncoderType::JPEG: return MPP_VIDEO_CodingMJPEG;
        default:                   return MPP_VIDEO_CodingUnused;
    }
}

MppFrameFormat MppEncoder::toMppFrameFormat(image_format_t format) {
    switch (format) {
        case IMAGE_FORMAT_RGB888:        return MPP_FMT_RGB888;
        case IMAGE_FORMAT_RGBA8888:      return MPP_FMT_RGBA8888;
        case IMAGE_FORMAT_YUV420SP_NV21: return MPP_FMT_YUV420SP_VU;
        case IMAGE_FORMAT_YUV420SP_NV12: return MPP_FMT_YUV420SP;
        default:                         return MPP_FMT_BUTT;
    }
}

const char* MppEncoder::typeName(MppEncoderType type) {
    switch (type) {
        case MppEncoderType::H264: return "h264";
        case MppEncoderType::H265: return "h265";
        case MppEncoderType::JPEG: return "jpeg";
        default:                   return "unknown";
    }
}

bool MppEncoder::init(MppEncoderType type,
                      int width,
                      int height,
                      int hor_stride,
                      int ver_stride,
                      const std::string& stream_id,
                      MppFrameFormat input_fmt,
                      int fps,
                      int bitrate) {
    destroy();

    type_      = type;
    coding_    = toMppCoding(type);
    input_fmt_ = input_fmt;
    width_     = width;
    height_    = height;
    hor_stride_ = hor_stride;
    ver_stride_ = ver_stride;
    fps_       = fps > 0 ? fps : kDefaultFps;
    bitrate_   = bitrate > 0 ? bitrate : (width_ * height_ * fps_) / 8;
    stream_id_ = stream_id;

    if (coding_ == MPP_VIDEO_CodingUnused || width_ <= 0 || height_ <= 0 ||
        hor_stride_ <= 0 || ver_stride_ <= 0) {
        LOG_ERROR("[MppEncoder] stream={} invalid init args type={} size={}x{}",
                  stream_id_, typeName(type_), width_, height_);
        return false;
    }

    switch (input_fmt_) {
        case MPP_FMT_BGR888:
        case MPP_FMT_RGB888:
            if ((width_ % 8) != 0) {
                LOG_ERROR("[MppEncoder] stream={} {} input requires width aligned to 8 for zero-copy, got {}",
                          stream_id_, typeName(type_), width_);
                return false;
            }
            break;
        case MPP_FMT_BGRA8888:
        case MPP_FMT_RGBA8888:
        case MPP_FMT_ARGB8888:
        case MPP_FMT_ABGR8888:
            if ((width_ % 8) != 0) {
                LOG_ERROR("[MppEncoder] stream={} {} input requires width aligned to 8 for zero-copy, got {}",
                          stream_id_, typeName(type_), width_);
                return false;
            }
            break;
        case MPP_FMT_YUV420SP:
        case MPP_FMT_YUV420SP_VU:
        case MPP_FMT_YUV420P:
            if ((width_ & 1) != 0 || (height_ & 1) != 0) {
                LOG_ERROR("[MppEncoder] stream={} yuv420 input requires even size, got {}x{}",
                          stream_id_, width_, height_);
                return false;
            }
            break;
        default:
            LOG_ERROR("[MppEncoder] stream={} unsupported input fmt={} for zero-copy encoder",
                      stream_id_, static_cast<int>(input_fmt_));
            return false;
    }

    MPP_RET ret = mpp_create(&ctx_, &mpi_);
    if (ret != MPP_OK) {
        LOG_ERROR("[MppEncoder] stream={} mpp_create failed ret={}", stream_id_, static_cast<int>(ret));
        return false;
    }

    ret = mpp_init(ctx_, MPP_CTX_ENC, coding_);
    if (ret != MPP_OK) {
        LOG_ERROR("[MppEncoder] stream={} mpp_init failed ret={}", stream_id_, static_cast<int>(ret));
        destroy();
        return false;
    }

    ret = mpp_enc_cfg_init(&cfg_);
    if (ret != MPP_OK || !cfg_) {
        LOG_ERROR("[MppEncoder] stream={} mpp_enc_cfg_init failed ret={}", stream_id_, static_cast<int>(ret));
        destroy();
        return false;
    }

    ret = mpi_->control(ctx_, MPP_ENC_GET_CFG, cfg_);
    if (ret != MPP_OK) {
        LOG_ERROR("[MppEncoder] stream={} MPP_ENC_GET_CFG failed ret={}", stream_id_, static_cast<int>(ret));
        destroy();
        return false;
    }

    if (!configure()) {
        destroy();
        return false;
    }

    if (coding_ == MPP_VIDEO_CodingAVC || coding_ == MPP_VIDEO_CodingHEVC) {
        MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
        ret = mpi_->control(ctx_, MPP_ENC_SET_HEADER_MODE, &header_mode);
        if (ret != MPP_OK) {
            LOG_ERROR("[MppEncoder] stream={} set header mode failed ret={}",
                      stream_id_, static_cast<int>(ret));
            destroy();
            return false;
        }
    }

    LOG_INFO("[MppEncoder] stream={} initialized type={} size={}x{} fmt={} fps={} bitrate={}",
             stream_id_, typeName(type_), width_, height_, static_cast<int>(input_fmt_), fps_, bitrate_);
    initialized_ = true;
    return true;
}

bool MppEncoder::configure() {
    if (!cfg_ || !ctx_ || !mpi_) return false;

    mpp_enc_cfg_set_s32(cfg_, "prep:width", width_);
    mpp_enc_cfg_set_s32(cfg_, "prep:height", height_);
    mpp_enc_cfg_set_s32(cfg_, "prep:hor_stride", hor_stride_);
    mpp_enc_cfg_set_s32(cfg_, "prep:ver_stride", ver_stride_);
    mpp_enc_cfg_set_s32(cfg_, "prep:format", input_fmt_);
    mpp_enc_cfg_set_s32(cfg_, "prep:range", MPP_FRAME_RANGE_JPEG);

    mpp_enc_cfg_set_s32(cfg_, "rc:mode",
                        coding_ == MPP_VIDEO_CodingMJPEG ? MPP_ENC_RC_MODE_FIXQP : MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_u32(cfg_, "rc:max_reenc_times", 0);
    mpp_enc_cfg_set_u32(cfg_, "rc:super_mode", 0);
    mpp_enc_cfg_set_s32(cfg_, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(cfg_, "rc:fps_in_num", fps_);
    mpp_enc_cfg_set_s32(cfg_, "rc:fps_in_denom", 1);
    mpp_enc_cfg_set_s32(cfg_, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(cfg_, "rc:fps_out_num", fps_);
    mpp_enc_cfg_set_s32(cfg_, "rc:fps_out_denom", 1);
    mpp_enc_cfg_set_u32(cfg_, "rc:drop_mode", MPP_ENC_RC_DROP_FRM_DISABLED);
    mpp_enc_cfg_set_u32(cfg_, "rc:drop_thd", 20);
    mpp_enc_cfg_set_u32(cfg_, "rc:drop_gap", 1);
    mpp_enc_cfg_set_s32(cfg_, "rc:gop", coding_ == MPP_VIDEO_CodingMJPEG ? 1 : fps_ * 2);

    if (coding_ != MPP_VIDEO_CodingMJPEG) {
        mpp_enc_cfg_set_s32(cfg_, "rc:bps_target", bitrate_);
        mpp_enc_cfg_set_s32(cfg_, "rc:bps_max", bitrate_ * 17 / 16);
        mpp_enc_cfg_set_s32(cfg_, "rc:bps_min", bitrate_ * 15 / 16);
        mpp_enc_cfg_set_s32(cfg_, "rc:qp_init", -1);
        mpp_enc_cfg_set_s32(cfg_, "rc:qp_max", 51);
        mpp_enc_cfg_set_s32(cfg_, "rc:qp_min", 10);
        mpp_enc_cfg_set_s32(cfg_, "rc:qp_max_i", 51);
        mpp_enc_cfg_set_s32(cfg_, "rc:qp_min_i", 10);
        mpp_enc_cfg_set_s32(cfg_, "rc:qp_ip", 2);
        mpp_enc_cfg_set_s32(cfg_, "rc:fqp_min_i", 10);
        mpp_enc_cfg_set_s32(cfg_, "rc:fqp_max_i", 45);
        mpp_enc_cfg_set_s32(cfg_, "rc:fqp_min_p", 10);
        mpp_enc_cfg_set_s32(cfg_, "rc:fqp_max_p", 45);
    } else {
        mpp_enc_cfg_set_s32(cfg_, "jpeg:q_factor", 80);
        mpp_enc_cfg_set_s32(cfg_, "jpeg:qf_max", 99);
        mpp_enc_cfg_set_s32(cfg_, "jpeg:qf_min", 1);
    }

    mpp_enc_cfg_set_s32(cfg_, "codec:type", coding_);
    if (coding_ == MPP_VIDEO_CodingAVC) {
        mpp_enc_cfg_set_s32(cfg_, "h264:profile", 100);
        mpp_enc_cfg_set_s32(cfg_, "h264:level", 40);
        mpp_enc_cfg_set_s32(cfg_, "h264:cabac_en", 1);
        mpp_enc_cfg_set_s32(cfg_, "h264:cabac_idc", 0);
        mpp_enc_cfg_set_s32(cfg_, "h264:trans8x8", 1);
    } else if (coding_ == MPP_VIDEO_CodingHEVC) {
        mpp_enc_cfg_set_s32(cfg_, "h265:diff_cu_qp_delta_depth", 0);
    }

    const MPP_RET ret = mpi_->control(ctx_, MPP_ENC_SET_CFG, cfg_);
    if (ret != MPP_OK) {
        LOG_ERROR("[MppEncoder] stream={} MPP_ENC_SET_CFG failed ret={}",
                  stream_id_, static_cast<int>(ret));
        return false;
    }

    return true;
}

void MppEncoder::destroy() {
    initialized_ = false;
    pending_inputs_.clear();

    for (auto& [fd, buffer] : imported_buffers_) {
        (void)fd;
        if (buffer) {
            mpp_buffer_put(buffer);
        }
    }
    imported_buffers_.clear();

    if (cfg_) {
        mpp_enc_cfg_deinit(cfg_);
        cfg_ = nullptr;
    }

    if (ctx_) {
        mpi_->reset(ctx_);
        mpp_destroy(ctx_);
        ctx_ = nullptr;
        mpi_ = nullptr;
    }
}

MppBuffer MppEncoder::importExternalBuffer(const DmaImage& image) {
    auto it = imported_buffers_.find(image.fd);
    if (it != imported_buffers_.end()) {
        return it->second;
    }

    MppBufferInfo info = {};
    info.type = MPP_BUFFER_TYPE_EXT_DMA;
    info.fd   = image.fd;
    info.ptr  = image.virt_addr;
    info.size = image.size;
    info.index = -1;

    MppBuffer buffer = nullptr;
    const MPP_RET ret = mpp_buffer_import(&buffer, &info);
    if (ret != MPP_OK || !buffer) {
        LOG_WARN("[MppEncoder] stream={} import ext dma failed fd={} ret={}",
                 stream_id_, image.fd, static_cast<int>(ret));
        return nullptr;
    }

    imported_buffers_.emplace(image.fd, buffer);
    return buffer;
}

bool MppEncoder::encodeImage(const std::shared_ptr<DmaImage>& image,
                             int64_t pts,
                             std::vector<MppPacket>& out_packets) {
    if (!initialized_ || !ctx_ || !mpi_ || !image) return false;

    MppBuffer buffer = importExternalBuffer(*image);
    if (!buffer) return false;

    MppFrame frame = nullptr;
    if (mpp_frame_init(&frame) != MPP_OK || !frame) {
        LOG_WARN("[MppEncoder] stream={} mpp_frame_init failed", stream_id_);
        return false;
    }

    mpp_frame_set_width(frame, static_cast<RK_U32>(width_));
    mpp_frame_set_height(frame, static_cast<RK_U32>(height_));
    mpp_frame_set_hor_stride(frame, static_cast<RK_U32>(hor_stride_));
    mpp_frame_set_ver_stride(frame, static_cast<RK_U32>(ver_stride_));
    mpp_frame_set_fmt(frame, input_fmt_);
    mpp_frame_set_pts(frame, static_cast<RK_S64>(pts));
    mpp_frame_set_buf_size(frame, image->size);
    mpp_frame_set_buffer(frame, buffer);

    const MPP_RET ret = mpi_->encode_put_frame(ctx_, frame);
    mpp_frame_deinit(&frame);

    if (ret == MPP_OK) {
        pending_inputs_.push_back(image);
    } else if (ret != MPP_ERR_BUFFER_FULL) {
        LOG_WARN("[MppEncoder] stream={} encode_put_frame ret={}", stream_id_, static_cast<int>(ret));
        return false;
    }

    drainPackets(out_packets);
    return true;
}

bool MppEncoder::flush(std::vector<MppPacket>& out_packets) {
    if (!initialized_ || !ctx_ || !mpi_) return false;

    MppFrame frame = nullptr;
    if (mpp_frame_init(&frame) != MPP_OK || !frame) {
        LOG_WARN("[MppEncoder] stream={} flush frame init failed", stream_id_);
        return false;
    }

    mpp_frame_set_eos(frame, 1);
    const MPP_RET ret = mpi_->encode_put_frame(ctx_, frame);
    mpp_frame_deinit(&frame);

    if (ret != MPP_OK && ret != MPP_ERR_BUFFER_FULL) {
        LOG_WARN("[MppEncoder] stream={} flush encode_put_frame ret={}", stream_id_, static_cast<int>(ret));
        return false;
    }

    drainPackets(out_packets);
    return true;
}

bool MppEncoder::forceIdr() {
    if (!initialized_ || !ctx_ || !mpi_ || (coding_ != MPP_VIDEO_CodingAVC && coding_ != MPP_VIDEO_CodingHEVC)) {
        return false;
    }

    const MPP_RET ret = mpi_->control(ctx_, MPP_ENC_SET_IDR_FRAME, nullptr);
    if (ret != MPP_OK) {
        LOG_WARN("[MppEncoder] stream={} force idr failed ret={}", stream_id_, static_cast<int>(ret));
        return false;
    }

    return true;
}

void MppEncoder::drainPackets(std::vector<MppPacket>& out_packets) {
    if (!ctx_ || !mpi_) return;

    while (true) {
        MppPacket packet = nullptr;
        const MPP_RET ret = mpi_->encode_get_packet(ctx_, &packet);
        if (ret != MPP_OK || !packet) {
            break;
        }

        out_packets.push_back(packet);
        if (!pending_inputs_.empty()) {
            pending_inputs_.pop_front();
        }

        if (mpp_packet_get_eos(packet)) {
            break;
        }
    }
}

} // namespace media_agent