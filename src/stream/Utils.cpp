#include "stream/Utils.h"

#include <cstring>

namespace media_agent {

std::string ffmpegErrorString(int errnum) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, errbuf, sizeof(errbuf));
    return std::string(errbuf);
}

AVRational normalizedTimeBase(AVRational time_base) {
    if (time_base.num <= 0 || time_base.den <= 0) {
        return AVRational{1, 1000};
    }
    return time_base;
}

bool sameStreamCodecParams(const StreamCodecParams& left, const StreamCodecParams& right) {
    return left.codec_id == right.codec_id &&
        left.time_base.num == right.time_base.num &&
        left.time_base.den == right.time_base.den &&
        left.fps == right.fps &&
        left.bit_rate == right.bit_rate &&
        left.width == right.width &&
        left.height == right.height &&
        left.extradata == right.extradata;
}

bool copyExtradata(const std::vector<uint8_t>& source, AVCodecParameters* codecpar) {
    if (!codecpar) {
        return false;
    }
    if (source.empty()) {
        codecpar->extradata = nullptr;
        codecpar->extradata_size = 0;
        return true;
    }

    auto* extradata = static_cast<uint8_t*>(av_mallocz(source.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!extradata) {
        return false;
    }

    std::memcpy(extradata, source.data(), source.size());
    codecpar->extradata = extradata;
    codecpar->extradata_size = static_cast<int>(source.size());
    return true;
}

AVMediaType avMediaTypeFrom(MediaType media_type) {
    switch (media_type) {
        case MediaType::Audio:
            return AVMEDIA_TYPE_AUDIO;
        case MediaType::Video:
        default:
            return AVMEDIA_TYPE_VIDEO;
    }
}

std::shared_ptr<AVPacket> clonePacket(const AVPacket& packet) {
    AVPacket* cloned = av_packet_clone(&packet);
    if (!cloned) {
        return nullptr;
    }

    return std::shared_ptr<AVPacket>(cloned, [](AVPacket* pkt) {
        if (pkt) {
            av_packet_free(&pkt);
        }
    });
}

std::shared_ptr<AVPacket> clonePacket(const AVPacket* packet) {
    if (!packet) {
        return nullptr;
    }
    return clonePacket(*packet);
}

bool addOutputStream(AVFormatContext* format_context,
                     const RtspStreamSpec& spec,
                     OutputStreamState& output_state) {
    if (!format_context) {
        return false;
    }

    AVStream* stream = avformat_new_stream(format_context, nullptr);
    if (!stream) {
        return false;
    }

    stream->id = spec.input_stream_index;
    stream->time_base = normalizedTimeBase(spec.time_base);
    if (spec.media_type == MediaType::Video) {
        stream->avg_frame_rate = AVRational{spec.fps > 0 ? spec.fps : 25, 1};
    }

    AVCodecParameters* codecpar = stream->codecpar;
    codecpar->codec_type = avMediaTypeFrom(spec.media_type);
    codecpar->codec_id = spec.codec_id;
    codecpar->codec_tag = 0;
    codecpar->bit_rate = spec.bit_rate;
    codecpar->width = spec.width;
    codecpar->height = spec.height;
    codecpar->sample_rate = spec.sample_rate;
    codecpar->channels = spec.channels;
    if (!copyExtradata(spec.extradata, codecpar)) {
        return false;
    }

    output_state = OutputStreamState{spec, stream};
    return true;
}

bool buildOutputStreamMap(AVFormatContext* format_context,
                          const std::vector<RtspStreamSpec>& specs,
                          std::unordered_map<int, OutputStreamState>& stream_states) {
    stream_states.clear();
    for (const auto& spec : specs) {
        OutputStreamState output_state;
        if (!addOutputStream(format_context, spec, output_state)) {
            return false;
        }
        stream_states.emplace(spec.input_stream_index, std::move(output_state));
    }
    return true;
}

} // namespace media_agent