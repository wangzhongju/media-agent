#include "MppDecoder.h"
#include "common/Logger.h"

extern "C" {
#include <libavcodec/avcodec.h>    // AVCodecID, AVPacket
}

namespace media_agent {

namespace {

std::string frameErrInfoToString(RK_U32 errinfo) {
    if (errinfo == 0) {
        return "none";
    }

    std::string text;
    auto append_flag = [&text](const char* name) {
        if (!text.empty()) {
            text += "|";
        }
        text += name;
    };

    if (errinfo & MPP_FRAME_ERR_UNKNOW) {
        append_flag("unknown");
    }
    if (errinfo & MPP_FRAME_ERR_UNSUPPORT) {
        append_flag("unsupported");
    }
    if (errinfo & MPP_FRAME_ERR_DEC_INVALID) {
        append_flag("dec_invalid");
    }
    if (errinfo & MPP_FRAME_ERR_DEC_HW_ERR) {
        append_flag("dec_hw_err");
    }
    if (errinfo & MPP_FRAME_ERR_DEC_MISS_REF) {
        append_flag("dec_miss_ref");
    }

    if (text.empty()) {
        text = "unmapped";
    }

    return text;
}

} // namespace

// ── 静态辅助：AVCodecID → MppCodingType ─────────────────────
MppCodingType MppDecoder::avCodecIdToMppCoding(int codec_id) {
    switch (codec_id) {
        case AV_CODEC_ID_H264:  return MPP_VIDEO_CodingAVC;
        case AV_CODEC_ID_HEVC:  return MPP_VIDEO_CodingHEVC;
        case AV_CODEC_ID_VP8:   return MPP_VIDEO_CodingVP8;
        case AV_CODEC_ID_VP9:   return MPP_VIDEO_CodingVP9;
        default:                return MPP_VIDEO_CodingUnused;
    }
}

// ── init ─────────────────────────────────────────────────────
bool MppDecoder::init(MppCodingType coding,
                      const uint8_t* extradata, int extra_size,
                      const std::string& stream_id) {
    stream_id_ = stream_id;

    // 创建 MPP 上下文
    MPP_RET ret = mpp_create(&ctx_, &mpi_);
    if (ret != MPP_OK) {
        LOG_ERROR("[MppDecoder] stream={} mpp_create failed ret={}", stream_id_, (int)ret);
        return false;
    }

    // 开启逐包解析模式（每个 AVPacket 含完整 Annex-B 码流）
    RK_U32 need_split = 1;
    mpi_->control(ctx_, MPP_DEC_SET_PARSER_SPLIT_MODE, &need_split);

    ret = mpp_init(ctx_, MPP_CTX_DEC, coding);
    if (ret != MPP_OK) {
        LOG_ERROR("[MppDecoder] stream={} mpp_init failed ret={}", stream_id_, (int)ret);
        mpp_destroy(ctx_);
        ctx_ = nullptr;
        mpi_ = nullptr;
        return false;
    }

    // 发送 SPS/PPS extradata（H.264 avcc → Annex-B 由 MPP 自行处理）
    if (extradata && extra_size > 0) {
        MppPacket extra_pkt = nullptr;
        mpp_packet_init(&extra_pkt,
                        const_cast<uint8_t*>(extradata),
                        static_cast<size_t>(extra_size));
        mpp_packet_set_extra_data(extra_pkt);
        mpi_->decode_put_packet(ctx_, extra_pkt);
        mpp_packet_deinit(&extra_pkt);
        LOG_DEBUG("[MppDecoder] stream={} sent extradata {} bytes", stream_id_, extra_size);
    }

    LOG_INFO("[MppDecoder] stream={} initialized coding={}", stream_id_, (int)coding);
    return true;
}

// ── destroy ──────────────────────────────────────────────────
void MppDecoder::destroy() {
    if (ctx_) {
        mpi_->reset(ctx_);
        mpp_destroy(ctx_);
        ctx_ = nullptr;
        mpi_ = nullptr;
    }
}

// ── submitPacket ─────────────────────────────────────────────
bool MppDecoder::submitPacket(AVPacket* pkt, std::vector<MppFrame>& out_frames) {
    if (!ctx_ || !pkt) return false;

    MppPacket mpp_pkt = nullptr;
    mpp_packet_init(&mpp_pkt, pkt->data, static_cast<size_t>(pkt->size));
    mpp_packet_set_pts(mpp_pkt, static_cast<RK_S64>(pkt->pts));
    mpp_packet_set_dts(mpp_pkt, static_cast<RK_S64>(pkt->dts));

    MPP_RET ret = mpi_->decode_put_packet(ctx_, mpp_pkt);
    mpp_packet_deinit(&mpp_pkt);

    if (ret != MPP_OK && ret != MPP_ERR_BUFFER_FULL) {
        LOG_WARN("[MppDecoder] stream={} decode_put_packet ret={}", stream_id_, (int)ret);
        return false;
    }

    // 每喂一个包后取出所有已解码帧
    drainFrames(out_frames);
    return true;
}

// ── drainFrames ──────────────────────────────────────────────
void MppDecoder::drainFrames(std::vector<MppFrame>& out) {
    // MPP 异步解码：一个包可能产生 0 或多帧
    while (true) {
        MppFrame mpp_frame = nullptr;
        MPP_RET ret = mpi_->decode_get_frame(ctx_, &mpp_frame);
        if (ret != MPP_OK || mpp_frame == nullptr) break;

        const bool is_eos         = (mpp_frame_get_eos(mpp_frame)         != 0);
        const RK_U32 errinfo      = mpp_frame_get_errinfo(mpp_frame);
        const bool is_err         = (errinfo != 0);
        const bool is_discard     = (mpp_frame_get_discard(mpp_frame)     != 0);
        const bool is_info_change = (mpp_frame_get_info_change(mpp_frame) != 0);

        if (is_info_change) {
            // MPP 首次解析 SPS/PPS 后发出 info-change 通知帧，
            // 该帧不含像素缓冲区（mpp_frame_get_buffer 返回 null），
            // 必须回复 INFO_CHANGE_READY 才能继续输出解码帧。
            LOG_INFO("[MppDecoder] stream={} info change: {}x{} stride={}x{}",
                     stream_id_,
                     mpp_frame_get_width(mpp_frame),
                     mpp_frame_get_height(mpp_frame),
                     mpp_frame_get_hor_stride(mpp_frame),
                     mpp_frame_get_ver_stride(mpp_frame));
            mpi_->control(ctx_, MPP_DEC_SET_INFO_CHANGE_READY, nullptr);
            mpp_frame_deinit(&mpp_frame);
        } else if (!is_eos && !is_err && !is_discard) {
            // 有效帧：追加到输出列表，调用方负责 mpp_frame_deinit
            out.push_back(mpp_frame);
        } else {
            if (is_err) {
                LOG_WARN("[MppDecoder] stream={} mpp frame error, skip errinfo=0x{:x} ({}) discard={} pts={} dts={} poc={}",
                         stream_id_,
                         errinfo,
                         frameErrInfoToString(errinfo),
                         is_discard,
                         mpp_frame_get_pts(mpp_frame),
                         mpp_frame_get_dts(mpp_frame),
                         mpp_frame_get_poc(mpp_frame));
            } else if (is_discard) {
                LOG_WARN("[MppDecoder] stream={} mpp frame discard, skip pts={} dts={} poc={}",
                         stream_id_,
                         mpp_frame_get_pts(mpp_frame),
                         mpp_frame_get_dts(mpp_frame),
                         mpp_frame_get_poc(mpp_frame));
            }
            // EOS/ERR 帧：此处直接 deinit，不交给调用方
            mpp_frame_deinit(&mpp_frame);
        }

        if (is_eos) break;
    }
}

} // namespace media_agent

