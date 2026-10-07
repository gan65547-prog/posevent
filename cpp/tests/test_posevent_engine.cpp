#include "posevent_engine.h"
#include <cassert>
#include <iostream>

int main()
{
    ZoneDefinition zone;
    zone.geometry.id = 9;
    zone.geometry.name = "person_zone";
    zone.geometry.type = regionType::rect;
    zone.geometry.rx = 0.2F; zone.geometry.ry = 0.2F;
    zone.geometry.rw = 0.6F; zone.geometry.rh = 0.6F;
    PoseventEngine engine({zone});
    const std::vector<Detection> person{{0.3F, 0.3F, 0.1F, 0.3F, 0.9F, "person"}};
    assert(!engine.process(person, 1).overlays[0].active);
    assert(!engine.process(person, 2).overlays[0].active);
    const auto result = engine.process(person, 3);
    assert(result.events.size() == 1);
    assert(result.events[0].type == "zone_occupied");
    assert(result.overlays[0].active);
    assert(result.overlays[0].red == 255 && result.overlays[0].green == 0);
    const auto during_occlusion = engine.process({}, 4);
    assert(during_occlusion.overlays[0].active);
    std::cout << "posevent engine: PASS\n";
}
