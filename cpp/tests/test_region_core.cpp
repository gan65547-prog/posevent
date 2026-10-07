#include "region/region_resolver.h"
#include "region/region_router.h"

#include <cassert>
#include <iostream>

int main()
{
    regionResolver resolver;
    resolver.addRectRegion(1, "restricted", .2f, .2f, .4f, .4f);
    assert(resolver.resolveBox(.3f, .3f, .1f, .2f) == std::vector<uint32_t>{1});
    assert(resolver.resolveBox(.7f, .7f, .1f, .1f).empty());

    regionRouter router({3, 5, 10});
    router.addRegion(1);
    router.tick({1}, 1);
    router.tick({1}, 2);
    router.tick({1}, 3);
    assert(router.getRuntime(1)->state == regionState::active);
    assert(router.getRuntime(1)->lastEvent == regionEvent::onConfirm);
    for (uint64_t frame = 4; frame <= 8; ++frame) router.tick({}, frame);
    assert(router.getRuntime(1)->state == regionState::cooldown);
    assert(router.getRuntime(1)->lastEvent == regionEvent::onLeave);
    std::cout << "region core: PASS\n";
}
