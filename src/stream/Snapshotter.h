#pragma once

#include "pipeline/StreamTypes.h"

#include <string>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct SwsContext;

namespace media_agent {

class Snapshotter {
public:
    Snapshotter() = default;
    ~Snapshotter() { close(); }

    Snapshotter(const Snapshotter&) = delete;
    Snapshotter& operator=(const Snapshotter&) = delete;

    bool configure(const std::string& stream_id, const std::string& base_dir);
    bool saveJpeg(const FrameBundle& frame, const std::vector<DetectionObject>& objects);
    void close();

    bool isConfigured() const { return !stream_id_.empty() && !base_dir_.empty(); }
    const std::string& currentFileName() const { return snapshot_file_name_; }

private:
    static std::string buildSnapshotFileName(const std::string& stream_id, int64_t now_ms);
    static std::string makeHiddenSnapshotName(const std::string& visible_name);

    bool ensureEncoder(int width, int height);
    bool copyInputFrame(const DmaImage& image);
    bool convertInputFrame();
    void drawBoxes(const std::vector<DetectionObject>& objects);
    bool writeSnapshotFile(const std::string& relative_file_name);
    void releaseFrames();

    std::string      stream_id_;
    std::string      base_dir_;
    std::string      snapshot_file_name_;
    AVCodecContext*  codec_ctx_ = nullptr;
    SwsContext*      sws_ctx_ = nullptr;
    AVFrame*         input_frame_ = nullptr;
    AVFrame*         output_frame_ = nullptr;
    int              frame_width_ = 0;
    int              frame_height_ = 0;
};

} // namespace media_agent