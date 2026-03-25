#pragma once  // 防止头文件重复包含。

#include "pipeline/StreamTypes.h" // EncodedPacket / DetectionObject。

#include <cstdint> // int64_t。
#include <memory>  // std::shared_ptr。
#include <string>  // std::string。
#include <vector>  // SEI payload 字节数组。

namespace media_agent {
/**
 * SEI: Supplemental Enhancement Information 补充增强信息
 * 这是一种在H.264/H.265视频码流中嵌入自定义数据的技术或工具。
 * 它可以将GPS信息、时间戳、字幕、AI分析结果等元数据，以标准
 * 规范的方式“注入”到视频流中，并确保这些数据与特定视频帧保持同步
 */
// SEI 所属的视频编码类型。
enum class SeiCodecType {
    H264, // H.264。
    H265, // H.265 / HEVC。
};

// 一次 SEI 注入所需的业务上下文。
struct SeiPayloadContext {
    std::string stream_id;                  // 流 ID。
    int64_t frame_id = -1;                  // 帧 ID。
    int64_t pts = -1;                       // 帧 PTS。
    bool reused_cached_result = false;      // 是否复用了缓存结果。
    int64_t result_expire_at_mono_ms = 0;   // 结果过期时间。
    std::string algorithm_id;               // 算法 ID。
    std::vector<DetectionObject> objects;   // 目标列表。
};

// SEI payload 编码器接口。
class ISeiPayloadEncoder {
public:
    virtual ~ISeiPayloadEncoder() = default;
    virtual std::vector<uint8_t> encode(const SeiPayloadContext& context) const = 0;
};

// SEI 注入器接口。
class ISeiInjector {
public:
    virtual ~ISeiInjector() = default;

    // 把业务上下文编码成 SEI，并注入到源编码包前面。
    virtual bool inject(const EncodedPacket& source_packet,
                        SeiCodecType codec_type,
                        int nal_length_size,
                        const SeiPayloadContext& context,
                        std::shared_ptr<AVPacket>& output_packet) const = 0;
};

// 创建默认的“直接插入 SEI NAL”实现。
std::unique_ptr<ISeiInjector> createPassthroughSeiInjector();

} // namespace media_agent
