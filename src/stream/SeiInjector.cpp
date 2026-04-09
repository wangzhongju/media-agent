#include "stream/SeiInjector.h"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace media_agent {

namespace {

constexpr std::array<uint8_t, 4> kAnnexBStartCode{{0x00, 0x00, 0x00, 0x01}};
constexpr std::array<uint8_t, 16> kSeiUuid{{
    0x83, 0xA1, 0x61, 0xC4, 0x31, 0xA7, 0x4B, 0xD8,
    0xA6, 0x93, 0x52, 0x11, 0x3A, 0x41, 0x10, 0x7E,
}};

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

bool isAnnexB(const uint8_t* data, size_t size) {
    if (!data || size < 4) {
        return false;
    }
    return (size >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01) ||
           (size >= 3 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01);
}

void appendNalLength(std::vector<uint8_t>& out, size_t value, int nal_length_size) {
    for (int shift = (nal_length_size - 1) * 8; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

void appendBeU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendBeU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

uint16_t quantizeUnitToU16(float value) {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    const float scaled = std::round(clamped * 65535.0F);
    return static_cast<uint16_t>(scaled);
}

uint8_t quantizeUnitToU8(float value) {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    const float scaled = std::round(clamped * 255.0F);
    return static_cast<uint8_t>(scaled);
}

uint8_t encodeObjectId(int32_t object_id) {
    if (object_id < 0 || object_id > static_cast<int32_t>(std::numeric_limits<uint8_t>::max() - 1)) {
        return std::numeric_limits<uint8_t>::max();
    }
    return static_cast<uint8_t>(object_id);
}

void appendSeiLength(std::vector<uint8_t>& rbsp, size_t value) {
    while (value >= 0xFF) {
        rbsp.push_back(0xFF);
        value -= 0xFF;
    }
    rbsp.push_back(static_cast<uint8_t>(value));
}

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

std::vector<uint8_t> toAnnexBNal(const std::vector<uint8_t>& nal_unit) {
    std::vector<uint8_t> nal;
    nal.reserve(kAnnexBStartCode.size() + nal_unit.size());
    nal.insert(nal.end(), kAnnexBStartCode.begin(), kAnnexBStartCode.end());
    nal.insert(nal.end(), nal_unit.begin(), nal_unit.end());
    return nal;
}

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

class PassthroughSeiInjector final : public ISeiInjector {
public:
    bool inject(const EncodedPacket& source_packet,
                SeiCodecType codec_type,
                int nal_length_size,
                const SeiPayloadContext& context,
                std::shared_ptr<AVPacket>& output_packet) const override {
        if (!source_packet.packet || !source_packet.packet->data || source_packet.packet->size <= 0) {
            output_packet = source_packet.packet;
            return false;
        }
        if (context.objects.empty()) {
            output_packet = source_packet.packet;
            return true;
        }
        const auto payload = buildUserDataPayload(context);
        const auto sei_nal_unit = codec_type == SeiCodecType::H265
            ? buildH265SeiNal(payload)
            : buildH264SeiNal(payload);
        const bool source_is_annex_b = isAnnexB(source_packet.packet->data,
                                                static_cast<size_t>(source_packet.packet->size));
        const auto sei_nal = source_is_annex_b
            ? toAnnexBNal(sei_nal_unit)
            : toLengthPrefixedNal(sei_nal_unit, nal_length_size);
        if (sei_nal.empty()) {
            output_packet = source_packet.packet;
            return false;
        }

        auto packet = allocatePacket(sei_nal.size() + static_cast<size_t>(source_packet.packet->size));
        if (!packet) {
            output_packet = source_packet.packet;
            return false;
        }

        std::memcpy(packet->data, sei_nal.data(), sei_nal.size());
        std::memcpy(packet->data + sei_nal.size(),
                    source_packet.packet->data,
                    static_cast<size_t>(source_packet.packet->size));
        if (av_packet_copy_props(packet.get(), source_packet.packet.get()) < 0) {
            output_packet = source_packet.packet;
            return false;
        }
        output_packet = std::move(packet);
        return true;
    }
};

std::unique_ptr<ISeiInjector> createPassthroughSeiInjector() {
    return std::make_unique<PassthroughSeiInjector>();
}

} // namespace media_agent