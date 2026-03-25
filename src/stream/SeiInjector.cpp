#include "stream/SeiInjector.h" // SeiInjector 实现。

#include <algorithm> // std::clamp / std::min。
#include <array>   // 固定字节数组。
#include <cmath>   // std::round。
#include <cstring> // std::memcpy。
#include <limits>  // 数值范围。
#include <vector>  // 动态字节数组。

extern "C" {
#include <libavcodec/avcodec.h> // AVPacket。
}

namespace media_agent {

namespace {

// Annex-B 起始码。
constexpr std::array<uint8_t, 4> kAnnexBStartCode{{0x00, 0x00, 0x00, 0x01}};

// 自定义 user_data_unregistered SEI UUID。
constexpr std::array<uint8_t, 16> kSeiUuid{{
    0x83, 0xA1, 0x61, 0xC4, 0x31, 0xA7, 0x4B, 0xD8,
    0xA6, 0x93, 0x52, 0x11, 0x3A, 0x41, 0x10, 0x7E,
}};

// 申请一个指定大小的新 AVPacket。
std::shared_ptr<AVPacket> allocatePacket(size_t size) {
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        return nullptr;
    }
    if (av_new_packet(packet, static_cast<int>(size)) < 0) {
        av_packet_free(&packet);
        return nullptr;
    }
    return std::shared_ptr<AVPacket>(packet, [](AVPacket* pkt) {
        if (pkt) {
            av_packet_free(&pkt);
        }
    });
}

// 判断一个包是不是 Annex-B 格式。
bool isAnnexB(const uint8_t* data, size_t size) {
    if (!data || size < 4) {
        return false;
    }
    return (size >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01) ||
           (size >= 3 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01);
}

// 追加一个指定字节宽度的大端 NAL 长度字段。
void appendNalLength(std::vector<uint8_t>& out, size_t value, int nal_length_size) {
    for (int shift = (nal_length_size - 1) * 8; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

// 追加一个大端 16 位整数。
void appendBeU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

// 追加一个大端 32 位整数。
void appendBeU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

// 把 0~1 的浮点数量化到 uint16_t。
uint16_t quantizeUnitToU16(float value) {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    const float scaled = std::round(clamped * 65535.0F);
    return static_cast<uint16_t>(scaled);
}

// 把 0~1 的浮点数量化到 uint8_t。
uint8_t quantizeUnitToU8(float value) {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    const float scaled = std::round(clamped * 255.0F);
    return static_cast<uint8_t>(scaled);
}

// 把 object_id 编码成单字节表示。
uint8_t encodeObjectId(int32_t object_id) {
    if (object_id < 0 || object_id > static_cast<int32_t>(std::numeric_limits<uint8_t>::max() - 1)) {
        return std::numeric_limits<uint8_t>::max();
    }
    return static_cast<uint8_t>(object_id);
}

// 按 SEI 规范写入 payload size 字段。
void appendSeiLength(std::vector<uint8_t>& rbsp, size_t value) {
    while (value >= 0xFF) {
        rbsp.push_back(0xFF);
        value -= 0xFF;
    }
    rbsp.push_back(static_cast<uint8_t>(value));
}

// RBSP 转 EBSP，必要时插入 0x03 防止出现起始码仿冒。
std::vector<uint8_t> toEbsp(const std::vector<uint8_t>& rbsp) {
    std::vector<uint8_t> ebsp;
    ebsp.reserve(rbsp.size() + 8);
    int zero_count = 0;
    for (uint8_t byte : rbsp) {
        if (zero_count >= 2 && byte <= 0x03) {
            ebsp.push_back(0x03);
            zero_count = 0;
        }
        ebsp.push_back(byte);
        if (byte == 0x00) {
            ++zero_count;
        } else {
            zero_count = 0;
        }
    }
    return ebsp;
}

// 按项目自定义格式构造 user_data_unregistered payload。
std::vector<uint8_t> buildUserDataPayload(const SeiPayloadContext& context) {
    std::vector<uint8_t> payload;
    const uint8_t version = 2;
    const size_t object_count = std::min(context.objects.size(), static_cast<size_t>(std::numeric_limits<uint8_t>::max()));
    const uint32_t timestamp = context.pts <= 0 ? 0U : static_cast<uint32_t>(context.pts);
    payload.reserve(kSeiUuid.size() + 6 + object_count * 12);
    payload.insert(payload.end(), kSeiUuid.begin(), kSeiUuid.end());
    payload.push_back(version);
    payload.push_back(static_cast<uint8_t>(object_count));
    appendBeU32(payload, timestamp);

    for (size_t i = 0; i < object_count; ++i) {
        const auto& object = context.objects[i];
        payload.push_back(encodeObjectId(object.object_id()));
        appendBeU16(payload, static_cast<uint16_t>(object.type()));
        payload.push_back(quantizeUnitToU8(object.confidence()));
        appendBeU16(payload, quantizeUnitToU16(object.bbox().x()));
        appendBeU16(payload, quantizeUnitToU16(object.bbox().y()));
        appendBeU16(payload, quantizeUnitToU16(object.bbox().width()));
        appendBeU16(payload, quantizeUnitToU16(object.bbox().height()));
    }

    return payload;
}

// 构造 H.264 SEI NAL 单元。
std::vector<uint8_t> buildH264SeiNal(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> rbsp;
    appendSeiLength(rbsp, 5);
    appendSeiLength(rbsp, payload.size());
    rbsp.insert(rbsp.end(), payload.begin(), payload.end());
    rbsp.push_back(0x80);

    const auto ebsp = toEbsp(rbsp);
    std::vector<uint8_t> nal;
    nal.reserve(1 + ebsp.size());
    nal.push_back(0x06);
    nal.insert(nal.end(), ebsp.begin(), ebsp.end());
    return nal;
}

// 构造 H.265 SEI NAL 单元。
std::vector<uint8_t> buildH265SeiNal(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> rbsp;
    appendSeiLength(rbsp, 5);
    appendSeiLength(rbsp, payload.size());
    rbsp.insert(rbsp.end(), payload.begin(), payload.end());
    rbsp.push_back(0x80);

    const auto ebsp = toEbsp(rbsp);
    std::vector<uint8_t> nal;
    nal.reserve(2 + ebsp.size());
    nal.push_back(static_cast<uint8_t>(39 << 1));
    nal.push_back(0x01);
    nal.insert(nal.end(), ebsp.begin(), ebsp.end());
    return nal;
}

// 把裸 NAL 转成 Annex-B 格式。
std::vector<uint8_t> toAnnexBNal(const std::vector<uint8_t>& nal_unit) {
    std::vector<uint8_t> nal;
    nal.reserve(kAnnexBStartCode.size() + nal_unit.size());
    nal.insert(nal.end(), kAnnexBStartCode.begin(), kAnnexBStartCode.end());
    nal.insert(nal.end(), nal_unit.begin(), nal_unit.end());
    return nal;
}

// 把裸 NAL 转成长度前缀格式。
std::vector<uint8_t> toLengthPrefixedNal(const std::vector<uint8_t>& nal_unit, int nal_length_size) {
    if (nal_length_size < 1 || nal_length_size > 4) {
        return {};
    }

    const uint64_t max_nal_size = nal_length_size == 4
        ? static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
        : ((static_cast<uint64_t>(1) << (nal_length_size * 8)) - 1);
    if (nal_unit.size() > max_nal_size) {
        return {};
    }

    std::vector<uint8_t> nal;
    nal.reserve(static_cast<size_t>(nal_length_size) + nal_unit.size());
    appendNalLength(nal, nal_unit.size(), nal_length_size);
    nal.insert(nal.end(), nal_unit.begin(), nal_unit.end());
    return nal;
}

} // namespace

// 默认的直接注入实现。
class PassthroughSeiInjector final : public ISeiInjector {
public:
    bool inject(const EncodedPacket& source_packet,
                SeiCodecType codec_type,
                int nal_length_size,
                const SeiPayloadContext& context,
                std::shared_ptr<AVPacket>& output_packet) const override {
        // 源包无效时直接回退。
        if (!source_packet.packet || !source_packet.packet->data || source_packet.packet->size <= 0) {
            output_packet = source_packet.packet;
            return false;
        }

        // 没有检测目标时，不需要插 SEI。
        if (context.objects.empty()) {
            output_packet = source_packet.packet;
            return true;
        }

        // 构造 payload 和对应编码类型的 SEI NAL。
        const auto payload = buildUserDataPayload(context);
        const auto sei_nal_unit = codec_type == SeiCodecType::H265
            ? buildH265SeiNal(payload)
            : buildH264SeiNal(payload);

        // 根据原包格式决定输出是 Annex-B 还是长度前缀格式。
        const bool source_is_annex_b = isAnnexB(source_packet.packet->data,
                                                static_cast<size_t>(source_packet.packet->size));
        const auto sei_nal = source_is_annex_b
            ? toAnnexBNal(sei_nal_unit)
            : toLengthPrefixedNal(sei_nal_unit, nal_length_size);
        if (sei_nal.empty()) {
            output_packet = source_packet.packet;
            return false;
        }

        // 申请一个新包，把 SEI NAL 拼接到源包前面。
        auto packet = allocatePacket(sei_nal.size() + static_cast<size_t>(source_packet.packet->size));
        if (!packet) {
            output_packet = source_packet.packet;
            return false;
        }

        std::memcpy(packet->data, sei_nal.data(), sei_nal.size());
        std::memcpy(packet->data + sei_nal.size(),
                    source_packet.packet->data,
                    static_cast<size_t>(source_packet.packet->size));

        // 尽量复制原始包属性，例如时间戳、flags 等。
        if (av_packet_copy_props(packet.get(), source_packet.packet.get()) < 0) {
            output_packet = source_packet.packet;
            return false;
        }
        output_packet = std::move(packet);
        return true;
    }
};

// 工厂函数。
std::unique_ptr<ISeiInjector> createPassthroughSeiInjector() {
    return std::make_unique<PassthroughSeiInjector>();
}

} // namespace media_agent
