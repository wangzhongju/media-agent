#include "stream/Snapshotter.h"

#include "common/Logger.h"
#include "common/Time.h"
#include "common/Utils.h"
#include "stream/Utils.h"

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace media_agent {

namespace {

constexpr uint8_t kBoxLuma = 76;
constexpr uint8_t kBoxU = 85;
constexpr uint8_t kBoxV = 255;

int clampCoord(int value, int lower, int upper) {
    return std::max(lower, std::min(value, upper));
}

void paintPixelNv12(AVFrame* frame, int x, int y) {
    if (!frame || x < 0 || y < 0 || x >= frame->width || y >= frame->height) {
        return;
    }

    frame->data[0][y * frame->linesize[0] + x] = kBoxLuma;

    const int uv_x = (x / 2) * 2;
    const int uv_y = y / 2;
    uint8_t* uv_row = frame->data[1] + uv_y * frame->linesize[1];
    uv_row[uv_x] = kBoxU;
    uv_row[uv_x + 1] = kBoxV;
}

void drawHorizontalLine(AVFrame* frame, int x0, int x1, int y, int thickness) {
    for (int offset = 0; offset < thickness; ++offset) {
        const int yy = y + offset;
        for (int x = x0; x <= x1; ++x) {
            paintPixelNv12(frame, x, yy);
        }
    }
}

void drawVerticalLine(AVFrame* frame, int x, int y0, int y1, int thickness) {
    for (int offset = 0; offset < thickness; ++offset) {
        const int xx = x + offset;
        for (int y = y0; y <= y1; ++y) {
            paintPixelNv12(frame, xx, y);
        }
    }
}

} // namespace

std::string Snapshotter::buildSnapshotFileName(const std::string& stream_id, int64_t now_ms) {
    const std::string timestamp = makeTimestamp();
    const std::string date_dir = timestamp.substr(0, 8);
    const std::string millisecond_suffix = std::to_string(now_ms % 1000);
    const std::string stream_dir = sanitizeFileComponent(stream_id);
    const std::string file_name = stream_dir + "_" +
        timestamp + "_" + millisecond_suffix + ".jpg";
    return date_dir + "/" + stream_dir + "/" + file_name;
}

std::string Snapshotter::makeHiddenSnapshotName(const std::string& visible_name) {
    const std::filesystem::path visible_path(visible_name);
    const auto parent = visible_path.parent_path();
    const auto file_name = visible_path.filename().string();
    return (parent / ("." + file_name)).string();
}

bool Snapshotter::configure(const std::string& stream_id, const std::string& base_dir) {
    if (stream_id.empty() || base_dir.empty()) {
        return false;
    }

    if (stream_id_ != stream_id || base_dir_ != base_dir) {
        close();
    }

    stream_id_ = stream_id;
    base_dir_ = base_dir;
    return true;
}

bool Snapshotter::saveJpeg(const FrameBundle& frame,
                           const std::vector<DetectionObject>& objects) {
    if (!isConfigured() || !frame.decoded_image || objects.empty()) {
        return false;
    }

    const auto& image = *frame.decoded_image;
    if (image.virt_addr == nullptr || image.width <= 0 || image.height <= 0 ||
        image.width_stride < image.width || image.height_stride < image.height) {
        LOG_WARN("[Snapshotter] stream={} invalid image frame_id={} width={} height={} stride={}x{}",
                 stream_id_,
                 frame.frame_id,
                 image.width,
                 image.height,
                 image.width_stride,
                 image.height_stride);
        return false;
    }

    if (!ensureEncoder(image.width, image.height) || !copyInputFrame(image)) {
        return false;
    }

    drawBoxes(objects);
    return writeSnapshotFile(buildSnapshotFileName(stream_id_, systemNowMs()));
}

bool Snapshotter::ensureEncoder(int width, int height) {
    if (codec_ctx_ && width == frame_width_ && height == frame_height_) {
        return true;
    }

    close();

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec) {
        LOG_ERROR("[Snapshotter] stream={} find MJPEG encoder failed", stream_id_);
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        LOG_ERROR("[Snapshotter] stream={} alloc codec context failed", stream_id_);
        return false;
    }

    codec_ctx_->codec_id = AV_CODEC_ID_MJPEG;
    codec_ctx_->codec_type = AVMEDIA_TYPE_VIDEO;
    codec_ctx_->pix_fmt = AV_PIX_FMT_YUVJ420P;
    codec_ctx_->width = width;
    codec_ctx_->height = height;
    codec_ctx_->time_base = AVRational{1, 25};
    codec_ctx_->framerate = AVRational{25, 1};

    int ret = avcodec_open2(codec_ctx_, codec, nullptr);
    if (ret < 0) {
        LOG_ERROR("[Snapshotter] stream={} open MJPEG encoder failed err={}",
                  stream_id_,
                  ffmpegErrorString(ret));
        close();
        return false;
    }

    input_frame_ = av_frame_alloc();
    output_frame_ = av_frame_alloc();
    if (!input_frame_ || !output_frame_) {
        LOG_ERROR("[Snapshotter] stream={} alloc frame failed", stream_id_);
        close();
        return false;
    }

    input_frame_->format = AV_PIX_FMT_NV12;
    input_frame_->width = width;
    input_frame_->height = height;
    ret = av_frame_get_buffer(input_frame_, 32);
    if (ret < 0) {
        LOG_ERROR("[Snapshotter] stream={} alloc NV12 frame buffer failed err={}",
                  stream_id_,
                  ffmpegErrorString(ret));
        close();
        return false;
    }

    output_frame_->format = codec_ctx_->pix_fmt;
    output_frame_->width = width;
    output_frame_->height = height;
    ret = av_frame_get_buffer(output_frame_, 32);
    if (ret < 0) {
        LOG_ERROR("[Snapshotter] stream={} alloc JPEG frame buffer failed err={}",
                  stream_id_,
                  ffmpegErrorString(ret));
        close();
        return false;
    }

    sws_ctx_ = sws_getContext(width,
                              height,
                              AV_PIX_FMT_NV12,
                              width,
                              height,
                              codec_ctx_->pix_fmt,
                              SWS_BILINEAR,
                              nullptr,
                              nullptr,
                              nullptr);
    if (!sws_ctx_) {
        LOG_ERROR("[Snapshotter] stream={} create swscale context failed", stream_id_);
        close();
        return false;
    }

    frame_width_ = width;
    frame_height_ = height;
    return true;
}

bool Snapshotter::copyInputFrame(const DmaImage& image) {
    if (!input_frame_) {
        return false;
    }

    int ret = av_frame_make_writable(input_frame_);
    if (ret < 0) {
        LOG_ERROR("[Snapshotter] stream={} make NV12 frame writable failed err={}",
                  stream_id_,
                  ffmpegErrorString(ret));
        return false;
    }

    const auto* src = static_cast<const uint8_t*>(image.virt_addr);
    if (!src) {
        return false;
    }

    const int y_stride = image.width_stride;
    const int uv_stride = image.width_stride;
    const int y_plane_size = image.width_stride * image.height_stride;
    const uint8_t* src_y = src;
    const uint8_t* src_uv = src + y_plane_size;

    for (int y = 0; y < image.height; ++y) {
        std::memcpy(input_frame_->data[0] + y * input_frame_->linesize[0],
                    src_y + y * y_stride,
                    static_cast<size_t>(image.width));
    }
    for (int y = 0; y < image.height / 2; ++y) {
        std::memcpy(input_frame_->data[1] + y * input_frame_->linesize[1],
                    src_uv + y * uv_stride,
                    static_cast<size_t>(image.width));
    }

    return true;
}

bool Snapshotter::convertInputFrame() {
    if (!input_frame_ || !output_frame_ || !sws_ctx_) {
        return false;
    }

    int ret = av_frame_make_writable(output_frame_);
    if (ret < 0) {
        LOG_ERROR("[Snapshotter] stream={} make JPEG frame writable failed err={}",
                  stream_id_,
                  ffmpegErrorString(ret));
        return false;
    }

    const int scaled_height = sws_scale(sws_ctx_,
                                        input_frame_->data,
                                        input_frame_->linesize,
                                        0,
                                        input_frame_->height,
                                        output_frame_->data,
                                        output_frame_->linesize);
    if (scaled_height != input_frame_->height) {
        LOG_ERROR("[Snapshotter] stream={} swscale convert failed expected_height={} actual_height={}",
                  stream_id_,
                  input_frame_->height,
                  scaled_height);
        return false;
    }

    return true;
}

void Snapshotter::drawBoxes(const std::vector<DetectionObject>& objects) {
    if (!input_frame_) {
        return;
    }

    const int width = input_frame_->width;
    const int height = input_frame_->height;
    const int thickness = std::max(2, std::min(width, height) / 180);

    for (const auto& object : objects) {
        const auto& box = object.bbox();
        const float half_width = box.width() * 0.5F;
        const float half_height = box.height() * 0.5F;
        const int left = clampCoord(static_cast<int>((box.x() - half_width) * width), 0, width - 1);
        const int top = clampCoord(static_cast<int>((box.y() - half_height) * height), 0, height - 1);
        const int right = clampCoord(static_cast<int>((box.x() + half_width) * width), 0, width - 1);
        const int bottom = clampCoord(static_cast<int>((box.y() + half_height) * height), 0, height - 1);
        if (right <= left || bottom <= top) {
            continue;
        }

        drawHorizontalLine(input_frame_, left, right, top, thickness);
        drawHorizontalLine(input_frame_, left, right, std::max(top, bottom - thickness + 1), thickness);
        drawVerticalLine(input_frame_, left, top, bottom, thickness);
        drawVerticalLine(input_frame_, std::max(left, right - thickness + 1), top, bottom, thickness);
    }
}

bool Snapshotter::writeSnapshotFile(const std::string& relative_file_name) {
    if (!codec_ctx_ || !input_frame_ || !output_frame_) {
        return false;
    }

    if (!convertInputFrame()) {
        return false;
    }

    avcodec_flush_buffers(codec_ctx_);

    int ret = avcodec_send_frame(codec_ctx_, output_frame_);
    if (ret < 0) {
        LOG_ERROR("[Snapshotter] stream={} send frame to MJPEG encoder failed err={}",
                  stream_id_,
                  ffmpegErrorString(ret));
        return false;
    }

    std::shared_ptr<AVPacket> packet(av_packet_alloc(), [](AVPacket* pkt) {
        if (pkt) {
            av_packet_free(&pkt);
        }
    });
    if (!packet) {
        LOG_ERROR("[Snapshotter] stream={} alloc packet failed", stream_id_);
        return false;
    }

    ret = avcodec_receive_packet(codec_ctx_, packet.get());
    if (ret < 0) {
        LOG_ERROR("[Snapshotter] stream={} receive MJPEG packet failed err={}",
                  stream_id_,
                  ffmpegErrorString(ret));
        return false;
    }

    const std::string tmp_relative_file_name = makeHiddenSnapshotName(relative_file_name);
    const std::filesystem::path tmp_path = std::filesystem::path(base_dir_) / tmp_relative_file_name;
    const std::filesystem::path final_path = std::filesystem::path(base_dir_) / relative_file_name;
    std::filesystem::create_directories(tmp_path.parent_path());

    std::ofstream output(tmp_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        LOG_ERROR("[Snapshotter] stream={} open snapshot file failed path={}",
                  stream_id_,
                  tmp_path.string());
        return false;
    }

    output.write(reinterpret_cast<const char*>(packet->data), packet->size);
    output.close();
    if (!output) {
        LOG_ERROR("[Snapshotter] stream={} write snapshot file failed path={}",
                  stream_id_,
                  tmp_path.string());
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec) {
        LOG_ERROR("[Snapshotter] stream={} rename snapshot failed: {} -> {} err={}",
                  stream_id_,
                  tmp_path.string(),
                  final_path.string(),
                  ec.message());
        return false;
    }

    LOG_DEBUG("[Snapshotter] stream={} saved snapshot file={}",
             stream_id_,
             final_path.string());
    snapshot_file_name_ = relative_file_name;
    return true;
}

void Snapshotter::releaseFrames() {
    if (input_frame_) {
        av_frame_free(&input_frame_);
    }
    if (output_frame_) {
        av_frame_free(&output_frame_);
    }
}

void Snapshotter::close() {
    releaseFrames();

    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }

    frame_width_ = 0;
    frame_height_ = 0;
    snapshot_file_name_.clear();
}

} // namespace media_agent