#include "osd_renderer.h"

#include <cassert>
#include <iostream>

int main() {
    ZoneDefinition zone;
    zone.geometry.id = 4;
    zone.geometry.name = "entrance";
    zone.geometry.type = regionType::rect;
    zone.geometry.rx = 0.25F; zone.geometry.ry = 0.25F;
    zone.geometry.rw = 0.5F; zone.geometry.rh = 0.5F;
    PoseventEngine engine({zone});
    const Detection person{0.4F, 0.3F, 0.1F, 0.3F, 0.9F, "person"};
    engine.process({person}, 1);
    engine.process({person}, 2);
    const auto result = engine.process({person}, 3);
    const auto svg = renderSvg(result, 1280, 720);
    assert(svg.find("data-frame=\"3\"") != std::string::npos);
    assert(svg.find("stroke=\"#ff0000\"") != std::string::npos);
    assert(svg.find("x=\"512\"") != std::string::npos);
    std::cout << "osd renderer: PASS\n";
}
