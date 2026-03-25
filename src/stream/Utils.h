#pragma once

#include "pipeline/StreamTypes.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mem.h>
}

namespace media_agent {

struct StreamCodecParams {
    AVCodecID            codec_id = AV_CODEC_ID_NONE;
    AVRational           time_base = AVRational{1, 1000};
    int                  fps = 25;
    int                  bit_rate = 0;
    int                  width = 0;
    int                  height = 0;
    std::vector<uint8_t> extradata;
};

struct RtspStreamSpec : StreamCodecParams {
    MediaType media_type = MediaType::Video;
    int       input_stream_index = -1;
    int       sample_rate = 0;
    int       channels = 0;
};

struct OutputStreamState {
    RtspStreamSpec spec;
    AVStream*      stream = nullptr;
};

std::string ffmpegErrorString(int errnum);

AVRational normalizedTimeBase(AVRational time_base);

bool sameStreamCodecParams(const StreamCodecParams& left, const StreamCodecParams& right);

bool copyExtradata(const std::vector<uint8_t>& source, AVCodecParameters* codecpar);

AVMediaType avMediaTypeFrom(MediaType media_type);

std::shared_ptr<AVPacket> clonePacket(const AVPacket& packet);

std::shared_ptr<AVPacket> clonePacket(const AVPacket* packet);

bool addOutputStream(AVFormatContext* format_context,
                     const RtspStreamSpec& spec,
                     OutputStreamState& output_state);

bool buildOutputStreamMap(AVFormatContext* format_context,
                          const std::vector<RtspStreamSpec>& specs,
                          std::unordered_map<int, OutputStreamState>& stream_states);

} // namespace media_agent