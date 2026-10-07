#include "region/region_router.h"

// Declaration-only policy (pipeline governance Phase 5 / D3): bodies moved
// out of region_router.h.  The enum name mappers, regionRuntime and the
// single-expression accessors (trackedRegionCount/allRuntimes/cfg) stay in
// the header.

regionRouter::regionRouter(const config& cfg)
    : cfg_(cfg)
{
}

std::vector<uint32_t> regionRouter::tick(const std::vector<uint32_t>& occupiedRegions,
                                          uint64_t frameSeq)
{
    std::vector<uint32_t> fired;

    // Build a quick lookup of occupied region IDs
    auto isOccupied = [&occupiedRegions](uint32_t rid) {
        for (auto id : occupiedRegions) {
            if (id == rid) {
                return true;
            }
        }
        return false;
    };

    // Step every known region
    for (auto& rt : regions_) {
        bool occ = isOccupied(rt.regionId);
        rt.lastEvent = step(rt, occ, frameSeq);
        if (rt.lastEvent != regionEvent::none) {
            fired.push_back(rt.regionId);
        }
    }

    // Auto-register newly occupied regions
    for (auto rid : occupiedRegions) {
        if (!findRegion(rid)) {
            regionRuntime rt;
            rt.regionId   = rid;
            rt.enteredAt  = frameSeq;
            rt.state      = regionState::preDetect;
            rt.stateCounter = 1;
            rt.lastEvent  = regionEvent::onEnter;
            regions_.push_back(rt);
            fired.push_back(rid);
        }
    }

    return fired;
}

regionRuntime* regionRouter::getRuntime(uint32_t regionId)
{
    for (auto& rt : regions_) {
        if (rt.regionId == regionId) {
            return &rt;
        }
    }
    return nullptr;
}

const regionRuntime* regionRouter::getRuntime(uint32_t regionId) const
{
    for (auto& rt : regions_) {
        if (rt.regionId == regionId) {
            return &rt;
        }
    }
    return nullptr;
}

regionState regionRouter::getState(uint32_t regionId) const
{
    auto* rt = getRuntime(regionId);
    return rt ? rt->state : regionState::idle;
}

void regionRouter::addRegion(uint32_t regionId)
{
    if (!findRegion(regionId)) {
        regionRuntime rt;
        rt.regionId = regionId;
        rt.state    = regionState::idle;
        regions_.push_back(rt);
    }
}

void regionRouter::reset()
{
    regions_.clear();
}

void regionRouter::setConfig(const config& cfg)
{
    cfg_ = cfg;
}

regionEvent regionRouter::step(regionRuntime& rt, bool occupied, uint64_t frameSeq)
{
    rt.stateCounter++;

    switch (rt.state) {

    case regionState::idle:
        if (occupied) {
            rt.state = regionState::preDetect;
            rt.stateCounter = 1;
            rt.enteredAt = frameSeq;
            return regionEvent::onEnter;
        }
        return regionEvent::none;

    case regionState::preDetect:
        if (occupied) {
            if (rt.stateCounter >= cfg_.confirmFrames) {
                rt.state = regionState::active;
                rt.stateCounter = 1;
                rt.activeAt = frameSeq;
                return regionEvent::onConfirm;
            }
        } else {
            rt.state = regionState::idle;
            rt.stateCounter = 0;
            return regionEvent::onFalse;
        }
        return regionEvent::none;

    case regionState::active:
        if (!occupied) {
            rt.state = regionState::postDetect;
            rt.stateCounter = 1;
            rt.lostAt = frameSeq;
            return regionEvent::onLost;
        }
        return regionEvent::none;

    case regionState::postDetect:
        if (occupied) {
            rt.state = regionState::active;
            rt.stateCounter = 1;
            return regionEvent::onReturn;
        }
        if (rt.stateCounter >= cfg_.lostFrames) {
            rt.state = regionState::cooldown;
            rt.stateCounter = 1;
            rt.leftAt = frameSeq;
            return regionEvent::onLeave;
        }
        return regionEvent::none;

    case regionState::cooldown:
        if (rt.stateCounter >= cfg_.cooldownFrames) {
            rt.state = regionState::idle;
            rt.stateCounter = 0;
            return regionEvent::onReady;
        }
        return regionEvent::none;
    }

    return regionEvent::none;
}

regionRuntime* regionRouter::findRegion(uint32_t regionId)
{
    for (auto& rt : regions_) {
        if (rt.regionId == regionId) {
            return &rt;
        }
    }
    return nullptr;
}
