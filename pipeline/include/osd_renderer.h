#pragma once

#include "posevent_engine.h"

#include <cstdint>
#include <string>

// Stateless renderer: all geometry and detection data comes from one result.
std::string renderSvg(const PoseventFrameResult& result, uint32_t width,
                      uint32_t height);
