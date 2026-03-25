#include "MppDecoder.h" // MppDecoder 实现。
#include "common/Logger.h" // 日志输出。

extern "C" {
#include <libavcodec/avcodec.h> // AVCodecID, AVPacket。
}

namespace media_agent {

namespace {

// 把 MPP 的 errinfo 位图转换成人类可读的文本。
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

// 把 FFmpeg codec_id 转成 MPP 编码类型。
MppCodingType MppDecoder::avCodecIdToMppCoding(int codec_id) {
    switch (codec_id) {
        case AV_CODEC_ID_H264: return MPP_VIDEO_CodingAVC;
        case AV_CODEC_ID_HEVC: return MPP_VIDEO_CodingHEVC;
        case AV_CODEC_ID_VP8: return MPP_VIDEO_CodingVP8;
        case AV_CODEC_ID_VP9: return MPP_VIDEO_CodingVP9;
        default: return MPP_VIDEO_CodingUnused;
    }
}

// 初始化解码器。
bool MppDecoder::init(MppCodingType coding,
                      const uint8_t* extradata,
                      int extra_size,
                      const std::string& stream_id) {
    stream_id_ = stream_id;

    // 创建 MPP 上下文和接口。
    MPP_RET ret = mpp_create(&ctx_, &mpi_);
    if (ret != MPP_OK) {
        LOG_ERROR("[MppDecoder] stream={} mpp_create failed ret={}", stream_id_, static_cast<int>(ret));
        return false;
    }

    // 开启按包切分模式。
    // 这样每个 AVPacket 都会被当成一个完整输入单元处理。
    RK_U32 need_split = 1;
    mpi_->control(ctx_, MPP_DEC_SET_PARSER_SPLIT_MODE, &need_split);

    // 初始化为解码模式。
    ret = mpp_init(ctx_, MPP_CTX_DEC, coding);
    if (ret != MPP_OK) {
        LOG_ERROR("[MppDecoder] stream={} mpp_init failed ret={}", stream_id_, static_cast<int>(ret));
        mpp_destroy(ctx_);
        ctx_ = nullptr;
        mpi_ = nullptr;
        return false;
    }

    // 如果上层提供了 SPS/PPS / VPS 等 extradata，就先喂给 MPP。
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

    LOG_INFO("[MppDecoder] stream={} initialized coding={}", stream_id_, static_cast<int>(coding));
    return true;
}

// 销毁解码器。
void MppDecoder::destroy() {
    if (ctx_) {
        mpi_->reset(ctx_);
        mpp_destroy(ctx_);
        ctx_ = nullptr;
        mpi_ = nullptr;
    }
}

// 提交一个编码包给 MPP，并尽可能取回解码结果。
bool MppDecoder::submitPacket(AVPacket* pkt, std::vector<MppFrame>& out_frames) {
    if (!ctx_ || !pkt) {
        return false;
    }

    // 用 AVPacket 的数据创建一个 MppPacket 视图。
    MppPacket mpp_pkt = nullptr;
    mpp_packet_init(&mpp_pkt, pkt->data, static_cast<size_t>(pkt->size));
    mpp_packet_set_pts(mpp_pkt, static_cast<RK_S64>(pkt->pts));
    mpp_packet_set_dts(mpp_pkt, static_cast<RK_S64>(pkt->dts));

    // 把包送进解码器。
    MPP_RET ret = mpi_->decode_put_packet(ctx_, mpp_pkt);
    mpp_packet_deinit(&mpp_pkt);

    // BUFFER_FULL 不算致命错误，说明当前内部队列满了，稍后再喂即可。
    if (ret != MPP_OK && ret != MPP_ERR_BUFFER_FULL) {
        LOG_WARN("[MppDecoder] stream={} decode_put_packet ret={}", stream_id_, static_cast<int>(ret));
        return false;
    }

    // 每喂一个包后，都尽量把当前已经解码出来的帧取干净。
    drainFrames(out_frames);
    return true;
}

// 从 MPP 中持续取帧。
void MppDecoder::drainFrames(std::vector<MppFrame>& out) {
    while (true) {
        MppFrame mpp_frame = nullptr;
        MPP_RET ret = mpi_->decode_get_frame(ctx_, &mpp_frame);
        if (ret != MPP_OK || mpp_frame == nullptr) {
            break;
        }

        const bool is_eos = (mpp_frame_get_eos(mpp_frame) != 0);
        const RK_U32 errinfo = mpp_frame_get_errinfo(mpp_frame);
        const bool is_err = (errinfo != 0);
        const bool is_discard = (mpp_frame_get_discard(mpp_frame) != 0);
        const bool is_info_change = (mpp_frame_get_info_change(mpp_frame) != 0);

        if (is_info_change) {
            // info-change 帧表示解码器拿到了新的流参数，例如宽高、步长变化。
            // 这类帧本身不是真正的视频图像，需要回复 INFO_CHANGE_READY 后继续。
            LOG_INFO("[MppDecoder] stream={} info change: {}x{} stride={}x{}",
                     stream_id_,
                     mpp_frame_get_width(mpp_frame),
                     mpp_frame_get_height(mpp_frame),
                     mpp_frame_get_hor_stride(mpp_frame),
                     mpp_frame_get_ver_stride(mpp_frame));
            mpi_->control(ctx_, MPP_DEC_SET_INFO_CHANGE_READY, nullptr);
            mpp_frame_deinit(&mpp_frame);
        } else if (!is_eos && !is_err && !is_discard) {
            // 正常图像帧交给上层，生命周期由上层负责 mpp_frame_deinit。
            out.push_back(mpp_frame);
        } else {
            // 错误帧、丢弃帧和 EOS 帧都不再向上游传递，直接在这里释放。
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
            mpp_frame_deinit(&mpp_frame);
        }

        if (is_eos) {
            break;
        }
    }
}

} // namespace media_agent