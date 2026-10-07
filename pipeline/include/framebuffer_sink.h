#pragma once

#include "frame_renderer.h"

#include <string>

class FramebufferSink {
public:
    explicit FramebufferSink(std::string device = "/dev/fb0") : device_(std::move(device)) {}
    bool present(const VideoFrame& frame, const PoseventFrameResult& result,
                 std::string& error);

private:
    std::string device_;
};
