#pragma once

#include <string>
#include <vector>

extern "C" {
#include <rockchip/mpp_buffer.h>
#include <rockchip/rk_mpi.h>
}

struct AVPacket;

namespace media_agent {

/**
 * MPP 硬件视频解码器
 *
 * 职责：
 *   - 管理 MppCtx / MppApi 生命周期
 *   - 将 AVPacket 裸码流（Annex-B）送入 MPP 硬件解码
 *   - 取出已解码的 MppFrame 并交给调用方处理
 *
 * 线程安全：单线程使用（每个 RTSPPuller 拥有独立实例）。
 */
class MppDecoder {
public:
    MppDecoder()  = default;
    ~MppDecoder() { destroy(); }

    MppDecoder(const MppDecoder&)            = delete;
    MppDecoder& operator=(const MppDecoder&) = delete;

    /**
     * 初始化 MPP 解码器。
     *
     * @param coding      MPP 编码类型（由 avCodecIdToMppCoding 转换）
     * @param extradata   SPS/PPS 数据（可为 nullptr）
     * @param extra_size  extradata 字节数
     * @param stream_id   日志标识
     * @return true 初始化成功
     */
    bool init(MppCodingType coding,
              const uint8_t* extradata, int extra_size,
              const std::string& stream_id);

    /** 销毁 MPP 解码器，释放所有资源。可安全重复调用。 */
    void destroy();

    /**
     * 将 AVPacket 送入 MPP，并取出所有当前可用的已解码帧。
     *
     * @param pkt        FFmpeg 读出的视频包（data/size 字段有效）
     * @param out_frames 输出：追加本次解码得到的 MppFrame 列表
     *                   调用方负责在使用完后调用 mpp_frame_deinit()
     * @return true 送包成功（或缓冲区满，非致命）
     */
    bool submitPacket(AVPacket* pkt, std::vector<MppFrame>& out_frames);

    /**
     * 将 FFmpeg AVCodecID 转换为 MPP 编码类型。
     *
     * @param codec_id  AVCodecID 枚举值（int 以避免包含 avcodec.h）
     * @return MPP 编码类型；不支持时返回 MPP_VIDEO_CodingUnused
     */
    static MppCodingType avCodecIdToMppCoding(int codec_id);

private:
    bool configureFrameBufferGroup(MppFrame frame);
    void releaseFrameBufferGroup();

    /**
     * 循环调用 decode_get_frame，将所有已解码帧追加到 out。
     * 有效帧追加后由调用方负责 deinit；EOS/ERR 帧在内部 deinit。
     */
    void drainFrames(std::vector<MppFrame>& out);

    MppCtx         ctx_                   = nullptr;
    MppApi*        mpi_                   = nullptr;
    MppBufferGroup frame_buffer_group_    = nullptr;
    size_t         frame_buffer_size_     = 0;
    int            frame_buffer_count_    = 0;
    std::string    stream_id_;
};

} // namespace media_agent

