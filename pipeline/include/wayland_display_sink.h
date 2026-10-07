#pragma once

#include "pipeline_contract.h"
#include "posevent_engine.h"

#include <cstdio>
#include <string>

// Display sink for boards where GNOME/Wayland owns the DRM scanout. The
// pipeline remains the owner of inference, OSD and presentation; ffplay is
// only the Wayland window backend selected by this sink. Runtime session
// values come from POSEVENT_WAYLAND_RUNTIME_DIR and POSEVENT_WAYLAND_DISPLAY;
// no user or sudo account is hard-coded here.
class WaylandDisplaySink {
public:
    WaylandDisplaySink(uint32_t width, uint32_t height);
    ~WaylandDisplaySink();

    bool present(const VideoFrame& frame, const PoseventFrameResult& result,
                 std::string& error);

private:
    uint32_t width_;
    uint32_t height_;
    FILE* pipe_ = nullptr;
};
