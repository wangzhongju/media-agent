#pragma once  // 防止头文件重复包含。

#include <string>  // std::string。
#include <vector>  // 存放解码输出帧。
#include <cstdint> // uint8_t。

extern "C" {
#include <rockchip/rk_mpi.h> // Rockchip MPP 解码接口。
}

struct AVPacket; // FFmpeg AVPacket 前向声明。

namespace media_agent {

// MPP 硬件解码器封装。
// 它负责把 FFmpeg 读到的编码包送入 Rockchip MPP，并取回解码后的 MppFrame。
class MppDecoder {
public:
    MppDecoder() = default;   // 默认构造。
    ~MppDecoder() { destroy(); } // 析构时自动释放 MPP 资源。

    MppDecoder(const MppDecoder&) = delete;            // 禁止拷贝。
    MppDecoder& operator=(const MppDecoder&) = delete; // 禁止赋值。

    // 初始化 MPP 解码器。
    bool init(MppCodingType coding,
              const uint8_t* extradata,
              int extra_size,
              const std::string& stream_id);

    // 销毁解码器，释放上下文和内部状态。
    void destroy();

    // 提交一个编码包，并尽可能取回当前可用的所有解码帧。
    bool submitPacket(AVPacket* pkt, std::vector<MppFrame>& out_frames);

    // 把 FFmpeg 的 codec_id 转成 MPP 所需的编码类型。
    static MppCodingType avCodecIdToMppCoding(int codec_id);

private:
    // 不断从 MPP 取帧，直到当前没有更多可用解码结果。
    void drainFrames(std::vector<MppFrame>& out);

    MppCtx ctx_ = nullptr;       // MPP 上下文句柄。
    MppApi* mpi_ = nullptr;      // MPP 操作接口表。
    std::string stream_id_;      // 当前流 ID，仅用于日志。
};

} // namespace media_agent
