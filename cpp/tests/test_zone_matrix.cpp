#include "posevent_engine.h"

#include <cassert>
#include <iostream>

static ZoneDefinition rect(uint32_t id, const char* name, float x, float y,
                           float w, float h) {
    ZoneDefinition zone;
    zone.geometry.id = id;
    zone.geometry.name = name;
    zone.geometry.type = regionType::rect;
    zone.geometry.rx = x; zone.geometry.ry = y;
    zone.geometry.rw = w; zone.geometry.rh = h;
    zone.classes = {"person"};
    zone.minConfidence = 0.5F;
    return zone;
}

int main() {
    const Detection inside{0.3F, 0.3F, 0.1F, 0.3F, 0.9F, "person"};
    const Detection outside{0.8F, 0.8F, 0.1F, 0.1F, 0.9F, "person"};
    const Detection lowConfidence{0.3F, 0.3F, 0.1F, 0.3F, 0.2F, "person"};
    const Detection wrongClass{0.3F, 0.3F, 0.1F, 0.3F, 0.9F, "car"};

    PoseventEngine engine({rect(1, "one", .2F, .2F, .4F, .5F),
                           rect(2, "two", .25F, .2F, .5F, .6F)});
    assert(engine.process({outside}, 1).events.empty());
    assert(engine.process({lowConfidence}, 2).events.empty());
    assert(engine.process({wrongClass}, 3).events.empty());
    assert(engine.process({inside}, 4).events.empty());
    assert(engine.process({inside}, 5).events.empty());
    const auto occupied = engine.process({inside}, 6);
    assert(occupied.events.size() == 2);
    assert(occupied.overlays[0].active && occupied.overlays[1].active);

    // One missed frame does not clear either active overlay.
    const auto occluded = engine.process({}, 7);
    assert(occluded.events.empty());
    assert(occluded.overlays[0].active && occluded.overlays[1].active);
    for (uint64_t frame = 8; frame <= 10; ++frame) {
        const auto result = engine.process({}, frame);
        assert(result.events.empty());
    }
    const auto empty = engine.process({}, 11);
    assert(empty.events.size() == 2);
    assert(!empty.overlays[0].active && !empty.overlays[1].active);
    std::cout << "zone matrix: PASS\n";
}
