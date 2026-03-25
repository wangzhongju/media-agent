#pragma once  // 防止头文件重复包含。

#include "pipeline/StreamTypes.h" // EncodedPacket / MediaType。

#include <cstdint>       // 整数类型。
#include <memory>        // std::shared_ptr。
#include <mutex>         // 互斥锁。
#include <string>        // std::string。
#include <unordered_map> // 流索引映射。
#include <vector>        // 输入轨道规格列表。

struct AVFormatContext; // FFmpeg 输出上下文前向声明。
struct AVStream;        // FFmpeg 输出流前向声明。

namespace media_agent {

// 一条输入轨道的发布规格。
// 这里保存的是“从 RTSP 输入侧学到”的视频或音频参数。
struct RtspStreamSpec {
    MediaType media_type = MediaType::Video; // 媒体类型。
    int input_stream_index = -1;             // 输入侧流索引。
    int codec_id = 0;                        // 编码类型。
    int time_base_num = 1;                   // 时间基分子。
    int time_base_den = 1000;                // 时间基分母。
    int width = 0;                           // 视频宽度。
    int height = 0;                          // 视频高度。
    int sample_rate = 0;                     // 音频采样率。
    int channels = 0;                        // 音频通道数。
    std::vector<uint8_t> extradata;          // SPS/PPS/VPS 等额外参数。
};

// RTSP 发布器。
// 它把 RTSPPuller 读到的音视频编码包，重新复用到新的 RTSP 输出流中。
class RtspPublisher {
public:
    RtspPublisher() = default;   // 默认构造。
    ~RtspPublisher() { close(); } // 析构时自动关闭输出流。

    RtspPublisher(const RtspPublisher&) = delete;            // 禁止拷贝。
    RtspPublisher& operator=(const RtspPublisher&) = delete; // 禁止赋值。

    // 根据输出地址和输入轨道规格重新配置发布器。
    bool configure(const std::string& stream_id,
                   const std::string& output_url,
                   const std::vector<RtspStreamSpec>& input_streams);

    // 写入一个编码包。
    // 如果 packet_override 非空，则优先发送替代包，例如插过 SEI 的视频包。
    bool writePacket(const EncodedPacket& packet,
                     const std::shared_ptr<AVPacket>& packet_override = nullptr);

    // 关闭当前发布器。
    void close();

private:
    // 每条输出轨道的状态。
    struct StreamState {
        int input_stream_index = -1; // 对应输入轨道索引。
        int input_time_base_num = 1; // 输入时间基分子。
        int input_time_base_den = 1000; // 输入时间基分母。
        AVStream* output_stream = nullptr; // 对应输出 AVStream。
    };

    bool openLocked(); // 在持锁状态下打开 RTSP 输出。
    bool writePacketLocked(const EncodedPacket& packet,
                           const std::shared_ptr<AVPacket>& source_packet); // 在持锁状态下写入一个包。
    void closeLocked(); // 在持锁状态下关闭资源。

    std::mutex mutex_;                           // 串行化 configure/write/close。
    std::string stream_id_;                      // 当前流 ID。
    std::string output_url_;                     // 输出 RTSP 地址。
    std::vector<RtspStreamSpec> input_streams_;  // 输入轨道规格缓存。
    std::unordered_map<int, StreamState> streams_; // 输入轨道索引到输出轨道状态的映射。
    AVFormatContext* format_context_ = nullptr;  // FFmpeg 输出上下文。
    bool configured_ = false;                    // 当前是否已配置完成。
};

} // namespace media_agent