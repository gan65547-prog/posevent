#include "annotated_rtsp_sink.h"

#include <sstream>

namespace {
std::string quote(const std::string& value) {
    std::string result = "'";
    for (const char character : value) {
        if (character == '\'') result += "'\\''";
        else result += character;
    }
    return result + "'";
}
}

AnnotatedRtspSink::AnnotatedRtspSink(std::string url, uint32_t width, uint32_t height)
    : url_(std::move(url)), width_(width), height_(height) {}

AnnotatedRtspSink::~AnnotatedRtspSink() { close(); }

bool AnnotatedRtspSink::open(std::string& error) {
    if (url_.empty() || width_ == 0 || height_ == 0) {
        error = "annotated RTSP URL and dimensions are required";
        return false;
    }
    std::ostringstream command;
    command << "ffmpeg -hide_banner -loglevel error -f rawvideo -pix_fmt rgb24 -s "
            << width_ << "x" << height_ << " -r 10 -i - -an -c:v libx264"
            << " -preset ultrafast -tune zerolatency -pix_fmt yuv420p"
            << " -f rtsp -rtsp_transport tcp " << quote(url_);
    pipe_ = popen(command.str().c_str(), "w");
    if (!pipe_) { error = "failed to start annotated RTSP encoder"; return false; }
    return true;
}

bool AnnotatedRtspSink::present(const VideoFrame& frame,
                                const PoseventFrameResult& result,
                                std::string& error) {
    if (!pipe_) { error = "annotated RTSP sink is not open"; return false; }
    const auto pixels = renderRgb(frame, result);
    if (pixels.size() != static_cast<size_t>(width_) * height_ * 3 ||
        fwrite(pixels.data(), 1, pixels.size(), pipe_) != pixels.size()) {
        error = "annotated RTSP encoder write failed";
        return false;
    }
    fflush(pipe_);
    return true;
}

void AnnotatedRtspSink::close() {
    if (pipe_) { pclose(pipe_); pipe_ = nullptr; }
}
