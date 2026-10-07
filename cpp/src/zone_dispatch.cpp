#include "zone_dispatch.h"

#include <algorithm>
#include <unordered_map>

ZoneDispatch::ZoneDispatch(std::vector<ZoneDefinition> zones, regionRouter::config config)
    : zones_(std::move(zones)), router_(config)
{
    for (const auto& zone : zones_) {
        resolver_.addRegion(zone.geometry);
        router_.addRegion(zone.geometry.id);
    }
}

std::vector<ZoneEvent> ZoneDispatch::process(const std::vector<Detection>& detections,
                                              uint64_t frame)
{
    std::vector<uint32_t> occupied;
    std::unordered_map<uint32_t, int> counts;
    for (const auto& detection : detections) {
        const float centerX = detection.x + detection.w * .5F;
        const float bottomY = detection.y + detection.h;
        for (const auto regionId : resolver_.resolvePoint(centerX, bottomY)) {
            const auto it = std::find_if(zones_.begin(), zones_.end(), [regionId](const auto& z) {
                return z.geometry.id == regionId;
            });
            if (it != zones_.end() && accepts(*it, detection)) {
                occupied.push_back(regionId);
                ++counts[regionId];
            }
        }
    }
    std::sort(occupied.begin(), occupied.end());
    occupied.erase(std::unique(occupied.begin(), occupied.end()), occupied.end());

    std::vector<ZoneEvent> events;
    for (const auto regionId : router_.tick(occupied, frame)) {
        const auto* runtime = router_.getRuntime(regionId);
        const auto zoneIt = std::find_if(zones_.begin(), zones_.end(), [regionId](const auto& z) {
            return z.geometry.id == regionId;
        });
        if (!runtime || zoneIt == zones_.end()) continue;
        if (runtime->lastEvent == regionEvent::onConfirm) {
            events.push_back({"zone_occupied", regionId, zoneIt->geometry.name, frame, counts[regionId]});
        } else if (runtime->lastEvent == regionEvent::onLeave) {
            events.push_back({"zone_empty", regionId, zoneIt->geometry.name, frame, 0});
        }
    }
    return events;
}

bool ZoneDispatch::accepts(const ZoneDefinition& zone, const Detection& detection) const
{
    return detection.confidence >= zone.minConfidence &&
           (zone.classes.empty() || std::find(zone.classes.begin(), zone.classes.end(), detection.label) != zone.classes.end());
}
