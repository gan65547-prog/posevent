#include "posevent_engine.h"

PoseventEngine::PoseventEngine(std::vector<ZoneDefinition> zones,
                               regionRouter::config config)
    : dispatch_(std::move(zones), config)
{
}

PoseventFrameResult PoseventEngine::process(
    const std::vector<Detection>& detections, uint64_t frame)
{
    PoseventFrameResult result;
    result.frameSeq = frame;
    result.detections = detections;
    result.events = dispatch_.process(detections, frame);
    result.overlays.reserve(dispatch_.zones().size());
    for (const auto& zone : dispatch_.zones()) {
        // Keep the zone highlighted during the exit debounce window. A short
        // detector miss must not make the displayed region flicker green.
        const auto state = dispatch_.state(zone.geometry.id);
        const bool active = state == regionState::active ||
                            state == regionState::postDetect;
        result.overlays.push_back({zone.geometry.id, zone.geometry.type, active,
                                   static_cast<uint8_t>(active ? 255 : 200),
                                   static_cast<uint8_t>(active ? 0 : 200), 0,
                                   zone.geometry});
    }
    return result;
}

PoseventFrameResult PoseventEngine::process(const DetectionFrame& frame)
{
    auto result = process(frame.detections, frame.frameSeq);
    result.timestampMs = frame.timestampMs;
    result.width = frame.width;
    result.height = frame.height;
    return result;
}
