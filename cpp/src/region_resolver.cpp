#include "region/region_resolver.h"

// regionResolver implementation.
//
// Declaration-only policy (pipeline governance Phase 5 / D3): the header
// keeps the data types, the inline spatial predicates (pointInRect /
// pointInPolygon are hot primitives and stay inline) and declarations;
// the regionResolver method bodies live in this TU.
//
// Regions are matched in add-order; fullFrame always matches.

void regionResolver::addRegion(const regionDef& r)
{
    regions_.push_back(r);
}

void regionResolver::addfullFrameRegion(uint32_t id, const std::string& name)
{
    regionDef r;
    r.id   = id;
    r.type = regionType::fullFrame;
    r.name = name;
    regions_.push_back(r);
}

void regionResolver::addRectRegion(uint32_t id, const std::string& name,
                                   float x, float y, float w, float h)
{
    regionDef r;
    r.id   = id;
    r.type = regionType::rect;
    r.name = name;
    r.rx   = x;
    r.ry   = y;
    r.rw   = w;
    r.rh   = h;
    regions_.push_back(r);
}

void regionResolver::addPolyRegion(uint32_t id, const std::string& name,
                                   const std::vector<point2f>& verts)
{
    regionDef r;
    r.id   = id;
    r.type = regionType::polygon;
    r.name = name;
    r.poly = verts;
    regions_.push_back(r);
}

// Find all region IDs that contain the point (x, y).
std::vector<uint32_t> regionResolver::resolvePoint(float x, float y) const
{
    std::vector<uint32_t> hits;
    for (const auto& r : regions_) {
        if (contains(r, x, y)) {
            hits.push_back(r.id);
        }
    }
    return hits;
}

// Find region IDs containing the center of a bounding box.
std::vector<uint32_t> regionResolver::resolveBox(float bx, float by,
                                                 float bw, float bh) const
{
    return resolvePoint(bx + bw * 0.5f, by + bh * 0.5f);
}

// Convenience: resolve to the first matching region, or 0.
uint32_t regionResolver::resolveFirst(float x, float y) const
{
    const auto hits = resolvePoint(x, y);
    return hits.empty() ? 0 : hits[0];
}

const std::vector<regionDef>& regionResolver::regions() const
{
    return regions_;
}

size_t regionResolver::regionCount() const
{
    return regions_.size();
}

void regionResolver::clear()
{
    regions_.clear();
}

bool regionResolver::contains(const regionDef& r, float x, float y) const
{
    switch (r.type) {
    case regionType::fullFrame:
        return true;
    case regionType::rect:
        return pointInRect(x, y, r.rx, r.ry, r.rw, r.rh);
    case regionType::polygon:
        return pointInPolygon(x, y, r.poly);
    }
    return false;
}
