#include "wayland_display_sink.h"

#include "frame_renderer.h"

#include <cerrno>
#include <cstring>
#include <cstdlib>

namespace {
const char* envOr(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : fallback;
}
}

WaylandDisplaySink::WaylandDisplaySink(uint32_t width, uint32_t height)
    : width_(width), height_(height) {}

WaylandDisplaySink::~WaylandDisplaySink() {
    if (pipe_ != nullptr) pclose(pipe_);
}

bool WaylandDisplaySink::present(const VideoFrame& frame,
                                 const PoseventFrameResult& result,
                                 std::string& error) {
    if (pipe_ == nullptr) {
        const std::string runtimeDir = envOr("POSEVENT_WAYLAND_RUNTIME_DIR", "");
        const std::string display = envOr("POSEVENT_WAYLAND_DISPLAY", "wayland-0");
        const std::string prefix = runtimeDir.empty()
            ? ""
            : "XDG_RUNTIME_DIR=" + runtimeDir + " ";
        const std::string command =
            "env -u LD_LIBRARY_PATH " + prefix + "WAYLAND_DISPLAY=" + display + " "
            "ffplay -hide_banner -loglevel error -fs -f rawvideo "
            "-pixel_format rgb24 -video_size " + std::to_string(width_) +
            "x" + std::to_string(height_) + " -framerate 15 -i -";
        pipe_ = popen(command.c_str(), "w");
        if (pipe_ == nullptr) {
            error = "failed to open Wayland display backend: " +
                    std::string(std::strerror(errno));
            return false;
        }
    }
    const auto pixels = renderRgb(frame, result);
    const size_t expected = static_cast<size_t>(width_) * height_ * 3U;
    if (pixels.size() != expected || fwrite(pixels.data(), 1, pixels.size(), pipe_) != pixels.size()) {
        error = "Wayland display backend write failed";
        return false;
    }
    fflush(pipe_);
    return true;
}
