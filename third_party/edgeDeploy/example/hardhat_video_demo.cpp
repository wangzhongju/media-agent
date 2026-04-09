#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <rockchip/mpp_buffer.h>
}

#include "edgeInfer.h"
#include "filesystem.hpp"
#include "image_drawing.h"
#include "image_utils.h"
#include "stream/MppDecoder.h"

namespace {

namespace fs = ghc::filesystem;

std::string dirname_of(const std::string& path) {
    const std::string::size_type pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return std::string(".");
    }
    if (pos == 0) {
        return std::string("/");
    }
    return path.substr(0, pos);
}

std::string get_install_root(const char* argv0) {
    char exe_path[PATH_MAX] = {0};
    const ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        return dirname_of(std::string(exe_path));
    }
    if (argv0 != nullptr) {
        return dirname_of(std::string(argv0));
    }
    return std::string(".");
}

bool path_exists(const std::string& path) {
    try {
        return fs::exists(fs::path(path));
    } catch (...) {
        return false;
    }
}

std::string first_existing_path(const std::vector<std::string>& candidates) {
    for (const std::string& candidate : candidates) {
        if (path_exists(candidate)) {
            return candidate;
        }
    }
    return candidates.empty() ? std::string() : candidates.front();
}

void release_image(image_buffer_t* image) {
    if (image != nullptr && image->virt_addr != nullptr) {
        free(image->virt_addr);
        image->virt_addr = nullptr;
    }
}

int clamp_int(int value, int lower, int upper) {
    if (value < lower) {
        return lower;
    }
    if (value > upper) {
        return upper;
    }
    return value;
}

const unsigned int kBoxColors[] = {
    COLOR_GREEN,
    COLOR_RED,
    COLOR_BLUE,
    COLOR_YELLOW,
    COLOR_ORANGE,
    COLOR_WHITE,
};

image_format_t mpp_format_to_image_format(MppFrameFormat fmt) {
    switch (fmt & MPP_FRAME_FMT_MASK) {
        case MPP_FMT_YUV420SP:
            return IMAGE_FORMAT_YUV420SP_NV12;
        case MPP_FMT_YUV420SP_VU:
            return IMAGE_FORMAT_YUV420SP_NV21;
        case MPP_FMT_RGB888:
            return IMAGE_FORMAT_RGB888;
        case MPP_FMT_RGBA8888:
            return IMAGE_FORMAT_RGBA8888;
        default:
            return static_cast<image_format_t>(-1);
    }
}

bool frame_to_image(MppFrame frame, image_buffer_t* image) {
    if (image == nullptr) {
        return false;
    }

    MppBuffer buffer = mpp_frame_get_buffer(frame);
    if (buffer == nullptr) {
        return false;
    }

    const image_format_t format = mpp_format_to_image_format(mpp_frame_get_fmt(frame));
    if (static_cast<int>(format) < 0) {
        return false;
    }

    std::memset(image, 0, sizeof(*image));
    image->width = static_cast<int>(mpp_frame_get_width(frame));
    image->height = static_cast<int>(mpp_frame_get_height(frame));
    image->width_stride = static_cast<int>(mpp_frame_get_hor_stride(frame));
    image->height_stride = static_cast<int>(mpp_frame_get_ver_stride(frame));
    image->format = format;
    image->virt_addr = static_cast<unsigned char*>(mpp_buffer_get_ptr(buffer));
    image->fd = mpp_buffer_get_fd(buffer);

    const int width_for_size = image->width_stride > 0 ? image->width_stride : image->width;
    const int height_for_size = image->height_stride > 0 ? image->height_stride : image->height;
    switch (format) {
        case IMAGE_FORMAT_RGB888:
            image->size = width_for_size * height_for_size * 3;
            break;
        case IMAGE_FORMAT_RGBA8888:
            image->size = width_for_size * height_for_size * 4;
            break;
        case IMAGE_FORMAT_YUV420SP_NV12:
        case IMAGE_FORMAT_YUV420SP_NV21:
            image->size = width_for_size * height_for_size * 3 / 2;
            break;
        default:
            image->size = 0;
            break;
    }

    return image->virt_addr != nullptr || image->fd > 0;
}

bool convert_frame_to_rgb(const image_buffer_t& source_image, image_buffer_t* rgb_image) {
    if (rgb_image == nullptr) {
        return false;
    }

    std::memset(rgb_image, 0, sizeof(*rgb_image));
    rgb_image->width = source_image.width;
    rgb_image->height = source_image.height;
    rgb_image->width_stride = source_image.width;
    rgb_image->height_stride = source_image.height;
    rgb_image->format = IMAGE_FORMAT_RGB888;
    rgb_image->size = get_image_size(rgb_image);
    rgb_image->virt_addr = static_cast<unsigned char*>(malloc(rgb_image->size));
    if (rgb_image->virt_addr == nullptr) {
        return false;
    }

    image_buffer_t src = source_image;
    if (convert_image(&src, rgb_image, nullptr, nullptr, 0) != RET_SUCCESS) {
        release_image(rgb_image);
        return false;
    }

    return true;
}

bool draw_normalized_results(image_buffer_t* image, const std::vector<object_result>& results) {
    if (image == nullptr || image->virt_addr == nullptr) {
        return false;
    }

    const int image_width = image->width;
    const int image_height = image->height;
    const int thickness = 2;
    const int font_size = 12;
    const size_t color_count = sizeof(kBoxColors) / sizeof(kBoxColors[0]);

    for (const object_result& result : results) {
        const float box_width = result.box.w * static_cast<float>(image_width);
        const float box_height = result.box.h * static_cast<float>(image_height);
        const float left = result.box.x * static_cast<float>(image_width) - box_width / 2.0f;
        const float top = result.box.y * static_cast<float>(image_height) - box_height / 2.0f;

        int draw_x = static_cast<int>(left + 0.5f);
        int draw_y = static_cast<int>(top + 0.5f);
        int draw_w = static_cast<int>(box_width + 0.5f);
        int draw_h = static_cast<int>(box_height + 0.5f);

        draw_x = clamp_int(draw_x, 0, image_width - 1);
        draw_y = clamp_int(draw_y, 0, image_height - 1);
        draw_w = clamp_int(draw_w, 1, image_width - draw_x);
        draw_h = clamp_int(draw_h, 1, image_height - draw_y);

        const unsigned int color = kBoxColors[static_cast<size_t>(result.class_id >= 0 ? result.class_id : 0) % color_count];
        draw_rectangle(image, draw_x, draw_y, draw_w, draw_h, color, thickness);

        char label[128] = {0};
        if (!result.class_name.empty()) {
            std::snprintf(label, sizeof(label), "%s %.2f", result.class_name.c_str(), result.prop);
        } else {
            std::snprintf(label, sizeof(label), "%d %.2f", result.class_id, result.prop);
        }
        draw_text(image, label, draw_x, draw_y > font_size ? draw_y - font_size : 0, color, font_size);
    }

    return true;
}

bool ensure_directory(const std::string& dir_path) {
    try {
        fs::create_directories(fs::path(dir_path));
        return true;
    } catch (const std::exception& e) {
        std::cerr << "failed to create directory: " << dir_path << ", error: " << e.what() << std::endl;
        return false;
    }
}

std::string frame_output_path(const std::string& output_dir, int saved_index) {
    std::ostringstream stream;
    stream << output_dir << "/frame_" << std::setfill('0') << std::setw(6) << saved_index << ".jpg";
    return stream.str();
}

bool save_frame_result(const image_buffer_t& decoded_image,
                       const std::vector<object_result>& results,
                       const std::string& output_dir,
                       int saved_index) {
    image_buffer_t rgb_image;
    std::memset(&rgb_image, 0, sizeof(rgb_image));

    if (!convert_frame_to_rgb(decoded_image, &rgb_image)) {
        std::cerr << "failed to convert decoded frame to RGB" << std::endl;
        return false;
    }

    if (!draw_normalized_results(&rgb_image, results)) {
        std::cerr << "failed to draw inference results" << std::endl;
        release_image(&rgb_image);
        return false;
    }

    const std::string output_path = frame_output_path(output_dir, saved_index);
    if (write_image(output_path.c_str(), &rgb_image) != RET_SUCCESS) {
        std::cerr << "failed to write result image: " << output_path << std::endl;
        release_image(&rgb_image);
        return false;
    }

    std::cout << "saved result image: " << output_path << std::endl;
    release_image(&rgb_image);
    return true;
}

bool process_decoded_frames(media_agent::MppDecoder& decoder,
                            EdgeInfer& infer,
                            filter_list_t& filters,
                            std::vector<MppFrame>& decoded_frames,
                            const std::string& output_dir,
                            int max_frames,
                            int* saved_count) {
    if (saved_count == nullptr) {
        return false;
    }

    for (MppFrame frame : decoded_frames) {
        image_buffer_t decoded_image;
        std::memset(&decoded_image, 0, sizeof(decoded_image));
        if (!frame_to_image(frame, &decoded_image)) {
            std::cerr << "skip unsupported MPP frame" << std::endl;
            mpp_frame_deinit(&frame);
            continue;
        }

        std::vector<object_result> results;
        if (infer.infer(decoded_image, filters, results) != RET_SUCCESS) {
            std::cerr << "infer failed on decoded frame" << std::endl;
            mpp_frame_deinit(&frame);
            return false;
        }

        if (!save_frame_result(decoded_image, results, output_dir, *saved_count)) {
            mpp_frame_deinit(&frame);
            return false;
        }

        ++(*saved_count);
        mpp_frame_deinit(&frame);
        if (*saved_count >= max_frames) {
            decoded_frames.clear();
            return true;
        }
    }

    decoded_frames.clear();
    return true;
}

bool feed_packet_to_decoder(media_agent::MppDecoder& decoder,
                            EdgeInfer& infer,
                            filter_list_t& filters,
                            AVPacket* packet,
                            const std::string& output_dir,
                            int max_frames,
                            int* saved_count) {
    std::vector<MppFrame> decoded_frames;
    if (!decoder.submitPacket(packet, decoded_frames)) {
        return false;
    }

    return process_decoded_frames(decoder, infer, filters, decoded_frames, output_dir, max_frames, saved_count);
}

bool flush_decoder(media_agent::MppDecoder& decoder,
                   EdgeInfer& infer,
                   filter_list_t& filters,
                   const std::string& output_dir,
                   int max_frames,
                   int* saved_count) {
    std::vector<MppFrame> decoded_frames;
    if (!decoder.flush(decoded_frames)) {
        return false;
    }

    return process_decoded_frames(decoder, infer, filters, decoded_frames, output_dir, max_frames, saved_count);
}

}

int main(int argc, char** argv) {
    const std::string install_root = get_install_root(argc > 0 ? argv[0] : nullptr);
    const std::string source_root = dirname_of(install_root);
    const std::string default_config = first_existing_path({
        install_root + "/weights/hardhatWare.json",
        source_root + "/example/weights/hardhatWare.demo.json.in",
        install_root + "/generated/hardhatWare.json",
    });
    const std::string default_video = first_existing_path({
        install_root + "/video/test0.mp4",
        source_root + "/example/video/test0.mp4",
    });
    const std::string default_output_dir = install_root + "/hardhat_video_demo_frames";

    const std::string config_path = argc > 1 ? argv[1] : default_config;
    const std::string video_path = argc > 2 ? argv[2] : default_video;
    const std::string output_dir = argc > 3 ? argv[3] : default_output_dir;
    const int max_frames = argc > 4 ? std::atoi(argv[4]) : 10;

    std::cout << "config    : " << config_path << std::endl;
    std::cout << "video     : " << video_path << std::endl;
    std::cout << "output dir: " << output_dir << std::endl;
    std::cout << "max frames: " << max_frames << std::endl;

    if (!ensure_directory(output_dir)) {
        return 1;
    }

    EdgeInfer infer;
    if (infer.init(config_path) != RET_SUCCESS) {
        std::cerr << "failed to init EdgeInfer with config: " << config_path << std::endl;
        return 1;
    }

    AVFormatContext* format_ctx = nullptr;
    if (avformat_open_input(&format_ctx, video_path.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "failed to open video: " << video_path << std::endl;
        return 1;
    }

    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        std::cerr << "failed to read stream info: " << video_path << std::endl;
        avformat_close_input(&format_ctx);
        return 1;
    }

    const int video_stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_index < 0) {
        std::cerr << "no video stream found in: " << video_path << std::endl;
        avformat_close_input(&format_ctx);
        return 1;
    }

    AVStream* video_stream = format_ctx->streams[video_stream_index];
    AVCodecParameters* codecpar = video_stream->codecpar;
    const MppCodingType coding = media_agent::MppDecoder::avCodecIdToMppCoding(codecpar->codec_id);
    if (coding == MPP_VIDEO_CodingUnused) {
        std::cerr << "unsupported codec id for MPP: " << codecpar->codec_id << std::endl;
        avformat_close_input(&format_ctx);
        return 1;
    }

    media_agent::MppDecoder decoder;
    if (!decoder.init(coding, codecpar->extradata, codecpar->extradata_size, "hardhat_video_demo")) {
        std::cerr << "failed to init MPP decoder" << std::endl;
        avformat_close_input(&format_ctx);
        return 1;
    }

    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) {
        std::cerr << "failed to alloc AVPacket" << std::endl;
        av_packet_free(&packet);
        avformat_close_input(&format_ctx);
        return 1;
    }

    filter_list_t filters;
    std::memset(&filters, 0, sizeof(filters));
    filters.confidence_threshold = 0.0f;

    int saved_count = 0;
    bool success = true;

    while (saved_count < max_frames && av_read_frame(format_ctx, packet) >= 0) {
        if (packet->stream_index != video_stream_index) {
            av_packet_unref(packet);
            continue;
        }

        if (!feed_packet_to_decoder(decoder, infer, filters, packet, output_dir, max_frames, &saved_count)) {
            success = false;
        }

        av_packet_unref(packet);
        if (!success) {
            break;
        }
    }

    if (success && saved_count < max_frames) {
        success = flush_decoder(decoder, infer, filters, output_dir, max_frames, &saved_count);
    }

    av_packet_free(&packet);
    avformat_close_input(&format_ctx);

    if (!success) {
        return 1;
    }

    std::cout << "saved " << saved_count << " result images to: " << output_dir << std::endl;
    return 0;
}