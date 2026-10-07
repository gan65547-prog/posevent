#!/usr/bin/env python3
"""Standalone oracle for region geometry and zone FSM.

The module deliberately has no external runtime, SDK, event-router, or Feishu
dependency.  A detection is ``{box: [x, y, w, h], class: str, score: float}``
with normalized top-left coordinates, matching the original pipeline.
"""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class Region:
    id: int
    name: str
    kind: str
    shape: tuple
    classes: frozenset[str]
    min_confidence: float = 0.0

    def contains(self, x: float, y: float) -> bool:
        if self.kind == "rect":
            rx, ry, rw, rh = self.shape
            return rx <= x <= rx + rw and ry <= y <= ry + rh
        vertices = self.shape
        inside = False
        j = len(vertices) - 1
        for i, (xi, yi) in enumerate(vertices):
            xj, yj = vertices[j]
            if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
                inside = not inside
            j = i
        return inside


def load_regions(path: str | Path) -> list[Region]:
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    result = []
    for item in data.get("regions", []):
        if item.get("type", "rect") == "polygon":
            shape = tuple((float(v["x"]), float(v["y"])) for v in item["vertices"])
            kind = "polygon"
        else:
            shape = tuple(float(item.get(k, 0.0)) for k in ("x", "y", "w", "h"))
            kind = "rect"
        result.append(Region(int(item["id"]), item.get("name", ""), kind, shape,
                             frozenset(item.get("classes", ["person"])),
                             float(item.get("min_confidence", 0.0))))
    return result


class ZoneMonitor:
    """3-frame entry / 5-frame exit debounce, matching the stable pipeline defaults."""

    def __init__(self, regions: Iterable[Region], entry_frames: int = 3,
                 exit_frames: int = 5, cooldown_frames: int = 10):
        self.regions = list(regions)
        self.entry_frames, self.exit_frames = max(1, entry_frames), max(1, exit_frames)
        self.cooldown_frames = max(1, cooldown_frames)
        self._state = {r.id: {"active": False, "seen": 0, "lost": 0, "cooldown": 0} for r in self.regions}

    def update(self, detections: Iterable[dict], frame: int, timestamp_ms: int) -> list[dict]:
        occupied = {r.id: 0 for r in self.regions}
        for det in detections:
            box = det.get("box", [])
            if len(box) != 4 or float(det.get("score", 1.0)) <= 0:
                continue
            cls = str(det.get("class", det.get("label", "person")))
            cx, cy = float(box[0]) + float(box[2]) / 2, float(box[1]) + float(box[3])
            for r in self.regions:
                if (not r.classes or cls in r.classes) and float(det.get("score", 1.0)) >= r.min_confidence and r.contains(cx, cy):
                    occupied[r.id] += 1
        events = []
        for r in self.regions:
            s = self._state[r.id]
            count = occupied[r.id]
            if count:
                s["seen"], s["lost"] = s["seen"] + 1, 0
                if not s["active"] and s["seen"] >= self.entry_frames and s["cooldown"] == 0:
                    s["active"] = True
                    events.append(self._event("zone_occupied", r, count, frame, timestamp_ms))
            else:
                s["seen"], s["lost"] = 0, s["lost"] + 1
                if s["active"] and s["lost"] >= self.exit_frames:
                    s["active"], s["cooldown"] = False, self.cooldown_frames
                    events.append(self._event("zone_empty", r, 0, frame, timestamp_ms))
            if s["cooldown"]:
                s["cooldown"] -= 1
        return events

    @staticmethod
    def _event(kind, region, count, frame, timestamp_ms):
        return {"event": kind, "region_id": region.id, "region_name": region.name,
                "count": count, "frame": frame, "timestamp_ms": timestamp_ms}
