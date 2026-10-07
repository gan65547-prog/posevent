#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ── regionResolver ───────────────────────────────────────────────────────────
//
// Pure spatial mapping: given a point or bounding box, determine which
// region(s) it belongs to.  Does NOT track state over time — that's the
// regionRouter's job.
//
// Region types:
//   fullFrame — always matches (MVP default, region 0)
//   rect      — axis-aligned rectangle
//   polygon   — arbitrary polygon (ray-casting)

// ── Region definition ─────────────────────────────────────────────────────

enum class regionType : uint32_t {
    fullFrame = 0,
    rect      = 1,
    polygon   = 2,
};

struct point2f {
    float x, y;
};

struct regionDef {
    uint32_t            id = 0;
    regionType          type = regionType::fullFrame;
    std::string         name;

    // rect params (when type == rect), normalized 0-1
    float               rx = 0.0f, ry = 0.0f;
    float               rw = 1.0f, rh = 1.0f;

    // polygon vertices (when type == polygon), normalized 0-1
    std::vector<point2f> poly;
};

// ── Spatial predicates ────────────────────────────────────────────────────

inline bool pointInRect(float px, float py,
                        float rx, float ry, float rw, float rh) {
    return px >= rx && px <= rx + rw && py >= ry && py <= ry + rh;
}

inline bool pointInPolygon(float px, float py,
                           const std::vector<point2f>& verts) {
    if (verts.size() < 3) {
        return false;
    }
    bool inside = false;
    size_t n = verts.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        float xi = verts[i].x, yi = verts[i].y;
        float xj = verts[j].x, yj = verts[j].y;
        if (((yi > py) != (yj > py)) &&
            (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

// ── Region resolver ───────────────────────────────────────────────────────

class regionResolver {
public:
    // Add a region definition.  Regions are checked in add-order.
    void addRegion(const regionDef& r);

    void addfullFrameRegion(uint32_t id = 0, const std::string& name = "full");

    void addRectRegion(uint32_t id, const std::string& name,
                       float x, float y, float w, float h);

    void addPolyRegion(uint32_t id, const std::string& name,
                       const std::vector<point2f>& verts);

    // Find all region IDs that contain the point (x, y).
    // Coordinates may be normalized 0-1 or pixel-space — caller decides.
    std::vector<uint32_t> resolvePoint(float x, float y) const;

    // Find region IDs containing the center of a bounding box.
    std::vector<uint32_t> resolveBox(float bx, float by, float bw, float bh) const;

    // Convenience: resolve to the first matching region, or 0.
    uint32_t resolveFirst(float x, float y) const;

    const std::vector<regionDef>& regions() const;
    size_t regionCount() const;

    void clear();

private:
    bool contains(const regionDef& r, float x, float y) const;

    std::vector<regionDef> regions_;
};
