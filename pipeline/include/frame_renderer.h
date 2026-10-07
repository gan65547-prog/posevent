#pragma once

#include "pipeline_contract.h"
#include "posevent_engine.h"

#include <cstdint>
#include <vector>

// CPU-only RGB renderer used by framebuffer/DRM adapters. It has no OpenCV or
// platform dependency and consumes only the current frame plus FrameResult.
std::vector<uint8_t> renderRgb(const VideoFrame& frame,
                               const PoseventFrameResult& result);
