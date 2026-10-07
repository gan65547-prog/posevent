#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ── regionRouter ──────────────────────────────────────────────────────────────
//
// Generic 5-state FSM for per-region occupancy tracking.  Decoupled from any
// domain (sports, OCR, industry) — downstream code reacts to transition
// events, the router only manages state.
//
// States:
//   idle       (0) — region empty, waiting
//   preDetect  (1) — object appeared; needs N consecutive frames to confirm
//   active     (2) — object confirmed present
//   postDetect (3) — object disappeared; needs M consecutive empty frames to
//                    confirm departure (handles brief occlusion)
//   cooldown   (4) — departure confirmed; cooldown before re-arm
//
// Transitions:
//   idle ──occupied──▶ preDetect ──N frames──▶ active
//   preDetect ──empty──▶ idle                    (false trigger)
//   active ──empty──▶ postDetect ──M frames──▶ cooldown
//   postDetect ──occupied──▶ active              (bounce / brief occlusion)
//   cooldown ──K frames──▶ idle                  (re-arm)

enum class regionState : uint32_t {
    idle       = 0,
    preDetect  = 1,
    active     = 2,
    postDetect = 3,
    cooldown   = 4,
};

inline const char* regionStateName(regionState s) {
    switch (s) {
    case regionState::idle:       return "idle";
    case regionState::preDetect:  return "preDetect";
    case regionState::active:     return "active";
    case regionState::postDetect: return "postDetect";
    case regionState::cooldown:   return "cooldown";
    }
    return "?";
}

// ── Transition event ──────────────────────────────────────────────────────

enum class regionEvent : uint32_t {
    none       = 0,
    onEnter    = 1,   // idle → preDetect (object entered region)
    onFalse    = 2,   // preDetect → idle (false trigger)
    onConfirm  = 3,   // preDetect → active (object confirmed)
    onLost     = 4,   // active → postDetect (object disappeared)
    onReturn   = 5,   // postDetect → active (object came back)
    onLeave    = 6,   // postDetect → cooldown (departure confirmed)
    onReady    = 7,   // cooldown → idle (re-armed)
};

inline const char* regionEventName(regionEvent e) {
    switch (e) {
    case regionEvent::none:       return "none";
    case regionEvent::onEnter:    return "enter";
    case regionEvent::onFalse:    return "false";
    case regionEvent::onConfirm:  return "confirm";
    case regionEvent::onLost:     return "lost";
    case regionEvent::onReturn:   return "return";
    case regionEvent::onLeave:    return "leave";
    case regionEvent::onReady:    return "ready";
    }
    return "?";
}

// ── Per-region runtime state ──────────────────────────────────────────────

struct regionRuntime {
    uint32_t    regionId = 0;
    regionState state    = regionState::idle;
    uint32_t    stateCounter = 0;   // frames spent in current state

    // Timestamps (frame seq numbers)
    uint64_t    enteredAt = 0;      // frame when idle→preDetect
    uint64_t    activeAt  = 0;      // frame when preDetect→active
    uint64_t    lostAt    = 0;      // frame when active→postDetect
    uint64_t    leftAt    = 0;      // frame when postDetect→cooldown

    // Event that just fired this frame (none if no transition)
    regionEvent lastEvent = regionEvent::none;
};

// ── Router ────────────────────────────────────────────────────────────────

// Method bodies follow the declaration-only policy (pipeline governance
// Phase 5 / D3) and live in region_router.cpp.

class regionRouter {
public:
    struct config {
        uint32_t confirmFrames  = 1;    // N frames to confirm entry
        uint32_t lostFrames     = 5;    // M frames to confirm departure
        uint32_t cooldownFrames = 10;   // K frames of cooldown before re-arm
    };

    explicit regionRouter(const config& cfg);
    regionRouter() = default;

    // Update all tracked regions for one frame tick.
    //   occupiedRegions: list of region IDs that contain a detection this frame.
    //   frameSeq: monotonic frame counter for timestamps.
    //
    // Returns list of regions that fired a transition event this frame.
    // Caller reads getRuntime(i)->lastEvent to decide what to emit.
    std::vector<uint32_t> tick(const std::vector<uint32_t>& occupiedRegions,
                                uint64_t frameSeq);

    // Access per-region runtime state.
    regionRuntime* getRuntime(uint32_t regionId);

    const regionRuntime* getRuntime(uint32_t regionId) const;

    // Query state for a region (idle if not tracked).
    regionState getState(uint32_t regionId) const;

    size_t trackedRegionCount() const { return regions_.size(); }

    const std::vector<regionRuntime>& allRuntimes() const { return regions_; }

    // Pre-register a region so the FSM tracks it even before any detection hits.
    // Without this, the router relies on auto-registration in tick(), which
    // only happens when a detection first falls inside the region geometry.
    void addRegion(uint32_t regionId);

    void reset();

    void setConfig(const config& cfg);

    const config& cfg() const { return cfg_; }

private:
    regionEvent step(regionRuntime& rt, bool occupied, uint64_t frameSeq);

    regionRuntime* findRegion(uint32_t regionId);

    config cfg_;
    std::vector<regionRuntime> regions_;
};
