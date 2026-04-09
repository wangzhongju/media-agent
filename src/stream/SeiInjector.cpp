#include "stream/SeiInjector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace media_agent {

namespace {


constexpr std::array<uint8_t, 4> kAnnexBStartCode{{0x00, 0x00, 0x00, 0x01}};
constexpr int kVersion = 1;
enum MspItemType : uint8_t {
    kMspItemTypeBbox = 1,
    kMspItemTypeTextOsd = 2,
};
constexpr std::array<uint8_t, 16> kSeiUuid{{
    0x4D, 0x45, 0x54, 0x41, 0x44, 0x41, 0x54, 0x41,
    0x53, 0x45, 0x49, 0x42, 0x59, 0x43, 0x48, 0x42,
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

uint16_t quantizeAngleToU16(float angle_degrees) {
    float normalized = std::fmod(angle_degrees, 360.0F);
    if (normalized < 0.0F) {
        normalized += 360.0F;
    }
    const float scaled = std::round((normalized / 360.0F) * 65535.0F);
    return static_cast<uint16_t>(scaled);
}

std::string detectionTypeToUtf8(const DetectionObject& object) {
    return object.class_name().empty() ? std::string("unknown") : object.class_name();
}

std::string osdTextToUtf8(const SeiTextOsdItem& item) {
    return item.text;
}

size_t appendMspBboxItem(std::vector<uint8_t>& payload, const DetectionObject& object) {
    constexpr size_t kFixedBboxPayloadSize = 18;
    const auto full_type_name = detectionTypeToUtf8(object);
    const size_t max_type_length = std::numeric_limits<uint8_t>::max() - kFixedBboxPayloadSize;
    const size_t type_length = std::min(full_type_name.size(), max_type_length);

    payload.push_back(kMspItemTypeBbox);
    payload.push_back(static_cast<uint8_t>(kFixedBboxPayloadSize + type_length));

    const uint32_t object_id = object.object_id();
    appendBeU16(payload, static_cast<uint16_t>(std::min<uint32_t>(object_id, std::numeric_limits<uint16_t>::max())));
    payload.push_back(static_cast<uint8_t>(type_length));
    payload.push_back(quantizeUnitToU8(object.confidence()));

    const auto& box = object.bbox();
    appendBeU16(payload, quantizeUnitToU16(box.cx()));
    appendBeU16(payload, quantizeUnitToU16(box.cy()));
    appendBeU16(payload, quantizeUnitToU16(box.width()));
    appendBeU16(payload, quantizeUnitToU16(box.height()));
    appendBeU16(payload, quantizeAngleToU16(box.angle()));
    appendBeU32(payload, 0);

    payload.insert(payload.end(), full_type_name.begin(), full_type_name.begin() + static_cast<std::ptrdiff_t>(type_length));
    return 1;
}

size_t appendMspTextOsdItem(std::vector<uint8_t>& payload, const SeiTextOsdItem& item) {
    constexpr size_t kFixedTextOsdPayloadSize = 20;
    const auto full_text = osdTextToUtf8(item);
    const size_t max_text_length = std::numeric_limits<uint8_t>::max() - kFixedTextOsdPayloadSize;
    const size_t text_length = std::min(full_text.size(), max_text_length);

    payload.push_back(kMspItemTypeTextOsd);
    payload.push_back(static_cast<uint8_t>(kFixedTextOsdPayloadSize + text_length));
    payload.push_back(item.flags);
    payload.push_back(item.style);
    appendBeU16(payload, quantizeUnitToU16(item.x));
    appendBeU16(payload, quantizeUnitToU16(item.y));
    appendBeU16(payload, quantizeUnitToU16(item.width));
    appendBeU16(payload, quantizeUnitToU16(item.height));
    payload.push_back(static_cast<uint8_t>(text_length));
    payload.push_back(item.reserved);
    appendBeU32(payload, item.text_color);
    appendBeU32(payload, item.bg_color);
    payload.insert(payload.end(), full_text.begin(), full_text.begin() + static_cast<std::ptrdiff_t>(text_length));
    return 1;
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

std::vector<uint8_t> buildMspV1UserDataPayload(const SeiMessageContext& context) {
    std::vector<uint8_t> payload;
    const size_t max_item_count = static_cast<size_t>(std::numeric_limits<uint8_t>::max());
    const size_t bbox_count = std::min(context.bbox_items.size(), max_item_count);
    const size_t text_osd_count = std::min(context.text_osd_items.size(), max_item_count);
    payload.reserve(kSeiUuid.size() + 4 + bbox_count * 32 + text_osd_count * 48);
    payload.insert(payload.end(), kSeiUuid.begin(), kSeiUuid.end());
    payload.push_back(kVersion);
    payload.push_back(0); // reserved1
    payload.push_back(0); // reserved2
    const size_t item_count_index = payload.size();
    payload.push_back(0);

    uint8_t item_count = 0;
    for (size_t i = 0; i < bbox_count && item_count < std::numeric_limits<uint8_t>::max(); ++i) {
        item_count += static_cast<uint8_t>(appendMspBboxItem(payload, context.bbox_items[i]));
    }
    for (size_t i = 0; i < text_osd_count && item_count < std::numeric_limits<uint8_t>::max(); ++i) {
        item_count += static_cast<uint8_t>(appendMspTextOsdItem(payload, context.text_osd_items[i]));
    }

    payload[item_count_index] = item_count;

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

class MspSeiInjector final : public ISeiInjector {
public:
    bool inject(const EncodedPacket& source_packet,
                SeiCodecType codec_type,
                int nal_length_size,
                const SeiMessageContext& context,
                std::shared_ptr<AVPacket>& output_packet) const override {
        if (!source_packet.packet || !source_packet.packet->data || source_packet.packet->size <= 0) {
            output_packet = source_packet.packet;
            return false;
        }
        if (!context.hasItems()) {
            output_packet = source_packet.packet;
            return true;
        }
        const auto payload = buildMspV1UserDataPayload(context);
        if (payload.empty()) {
            output_packet = source_packet.packet;
            return false;
        }
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

std::unique_ptr<ISeiInjector> createMspSeiInjector() {
    return std::make_unique<MspSeiInjector>();
}

} // namespace media_agent