#pragma once

#include "region/region_resolver.h"
#include "region/region_router.h"
#include "posevent/model_contract.h"

#include <cstdint>
#include <string>
#include <vector>

struct ZoneDefinition {
    regionDef geometry;
    std::vector<std::string> classes{"person"};
    float minConfidence = 0.0F;
};

struct ZoneEvent {
    std::string type;
    uint32_t regionId = 0;
    std::string regionName;
    uint64_t frame = 0;
    int detectionCount = 0;
};

class ZoneDispatch {
public:
    explicit ZoneDispatch(std::vector<ZoneDefinition> zones,
                          regionRouter::config config = {3, 5, 10});

    std::vector<ZoneEvent> process(const std::vector<Detection>& detections,
                                   uint64_t frame);
    const std::vector<ZoneDefinition>& zones() const { return zones_; }
    regionState state(uint32_t regionId) const { return router_.getState(regionId); }

private:
    bool accepts(const ZoneDefinition& zone, const Detection& detection) const;
    std::vector<ZoneDefinition> zones_;
    regionResolver resolver_;
    regionRouter router_;
};
