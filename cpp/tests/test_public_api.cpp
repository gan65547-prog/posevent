#include "posevent/model_contract.h"
#include "posevent/posevent_engine.h"
#include "posevent/zone_dispatch.h"

#include <cassert>
#include <iostream>

int main() {
    DetectionFrame frame;
    frame.frameSeq = 42;
    frame.detections.push_back({.1F, .1F, .1F, .2F, .9F, "person"});
    ZoneDefinition zone;
    zone.geometry.id = 1;
    zone.geometry.type = regionType::rect;
    zone.geometry.rx = 0.0F; zone.geometry.ry = 0.0F;
    zone.geometry.rw = 1.0F; zone.geometry.rh = 1.0F;
    PoseventEngine engine({zone});
    assert(engine.process(frame).frameSeq == 42);
    std::cout << "public API: PASS\n";
}
