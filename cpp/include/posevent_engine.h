#pragma once

#include "zone_dispatch.h"
#include "posevent/model_contract.h"

#include <cstdint>
#include <vector>

struct ZoneOverlay {
    uint32_t regionId = 0;
    regionType type = regionType::rect;
    bool active = false;
    uint8_t red = 200;
    uint8_t green = 200;
    uint8_t blue = 0;
    regionDef geometry;
};

struct PoseventFrameResult {
    uint64_t frameSeq = 0;
    uint64_t timestampMs = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<Detection> detections;
    std::vector<ZoneEvent> events;
    std::vector<ZoneOverlay> overlays;
};

// Standalone boundary for an external camera/inference/display pipeline.
class PoseventEngine {
public:
    explicit PoseventEngine(std::vector<ZoneDefinition> zones,
                            regionRouter::config config = {3, 5, 10});
    PoseventFrameResult process(const std::vector<Detection>& detections,
                                uint64_t frame);
    PoseventFrameResult process(const DetectionFrame& frame);
private:
    ZoneDispatch dispatch_;
};
