#!/usr/bin/env python3
import json, sys
from pathlib import Path

ALLOWED = {"id", "name", "type", "x", "y", "w", "h", "vertices", "classes", "min_confidence", "entry_frames", "exit_frames", "cooldown_frames"}

def fail(message): raise ValueError(message)
def num(value, field):
    if isinstance(value, bool) or not isinstance(value, (int, float)): fail(f"{field} must be numeric")
    return float(value)

def validate(data):
    if not isinstance(data, dict) or data.get("version") != "1": fail("version must be '1'")
    debounce = data.get("debounce", {})
    if not isinstance(debounce, dict): fail("debounce must be an object")
    for field in ("entry_frames", "exit_frames", "cooldown_frames"):
        if field in debounce and (not isinstance(debounce[field], int) or debounce[field] < 0): fail(f"debounce.{field} invalid")
    regions = data.get("regions")
    if not isinstance(regions, list) or not regions: fail("regions must be non-empty")
    ids = set()
    for i, region in enumerate(regions):
        if not isinstance(region, dict): fail(f"regions[{i}] must be an object")
        unknown = set(region) - ALLOWED
        if unknown: fail(f"regions[{i}] unsupported fields: {sorted(unknown)}")
        rid = region.get("id")
        if not isinstance(rid, int) or isinstance(rid, bool) or rid < 0 or rid in ids: fail(f"regions[{i}] invalid or duplicate id")
        ids.add(rid)
        if not isinstance(region.get("name"), str) or not region["name"]: fail(f"regions[{i}].name is required")
        typ = region.get("type")
        if typ == "rect":
            x, y, w, h = [num(region.get(k), f"regions[{i}].{k}") for k in ("x", "y", "w", "h")]
            if x < 0 or y < 0 or w <= 0 or h <= 0 or x + w > 1 or y + h > 1: fail(f"regions[{i}] rectangle out of range")
        elif typ == "polygon":
            vertices = region.get("vertices")
            if not isinstance(vertices, list) or len(vertices) < 3: fail(f"regions[{i}] polygon needs 3 vertices")
            for vertex in vertices:
                x, y = num(vertex.get("x"), "vertex.x"), num(vertex.get("y"), "vertex.y")
                if not 0 <= x <= 1 or not 0 <= y <= 1: fail(f"regions[{i}] vertex out of range")
        else: fail(f"regions[{i}].type must be rect or polygon")
        classes = region.get("classes", ["person"])
        if not isinstance(classes, list) or not all(isinstance(c, str) and c for c in classes): fail(f"regions[{i}].classes invalid")
        confidence = num(region.get("min_confidence", 0), "min_confidence")
        if not 0 <= confidence <= 1: fail(f"regions[{i}].min_confidence out of range")
        for field in ("entry_frames", "exit_frames", "cooldown_frames"):
            if field in region and (not isinstance(region[field], int) or region[field] < 0): fail(f"{field} invalid")

if __name__ == "__main__":
    try:
        raw = sys.stdin.read() if len(sys.argv) < 2 or sys.argv[1] == "-" else Path(sys.argv[1]).read_text()
        validate(json.loads(raw))
    except (OSError, json.JSONDecodeError, ValueError, IndexError) as error:
        print(f"invalid: {error}", file=sys.stderr); sys.exit(1)
    print("valid")
