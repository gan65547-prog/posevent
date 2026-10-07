#include "zone_dispatch.h"

#include <cassert>
#include <iostream>

int main()
{
    ZoneDefinition zone;
    zone.geometry.id = 7;
    zone.geometry.name = "restricted";
    zone.geometry.type = regionType::rect;
    zone.geometry.rx = .2F; zone.geometry.ry = .2F;
    zone.geometry.rw = .4F; zone.geometry.rh = .6F;
    ZoneDispatch dispatch({zone});
    const std::vector<Detection> person{{.3F, .3F, .1F, .3F, .9F, "person"}};
    assert(dispatch.process(person, 1).empty());
    assert(dispatch.process(person, 2).empty());
    auto entered = dispatch.process(person, 3);
    assert(entered.size() == 1 && entered[0].type == "zone_occupied");
    dispatch.process({}, 4); dispatch.process({}, 5); dispatch.process({}, 6); dispatch.process({}, 7);
    auto left = dispatch.process({}, 8);
    assert(left.size() == 1 && left[0].type == "zone_empty");
    std::cout << "zone dispatch: PASS\n";
}
