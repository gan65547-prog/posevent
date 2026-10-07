#pragma once

#include "frame_renderer.h"

#include <cstdio>
#include <string>

class AnnotatedRtspSink {
public:
    AnnotatedRtspSink(std::string url, uint32_t width, uint32_t height);
    ~AnnotatedRtspSink();
    AnnotatedRtspSink(const AnnotatedRtspSink&) = delete;
    bool open(std::string& error);
    bool present(const VideoFrame& frame, const PoseventFrameResult& result,
                 std::string& error);
    void close();

private:
    std::string url_;
    uint32_t width_;
    uint32_t height_;
    FILE* pipe_ = nullptr;
};
