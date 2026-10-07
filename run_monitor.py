#!/usr/bin/env python3
"""Run standalone zone monitoring from detection JSONL produced by a pipeline."""
import argparse, json, time
from pathlib import Path

from zone_monitor import ZoneMonitor, load_regions


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--regions", default="regions.json")
    p.add_argument("--detections-jsonl", help="Existing pipeline detections; avoids any SDK dependency")
    p.add_argument("--entry-frames", type=int, default=3)
    p.add_argument("--exit-frames", type=int, default=5)
    args = p.parse_args()
    monitor = ZoneMonitor(load_regions(args.regions), args.entry_frames, args.exit_frames)
    if not args.detections_jsonl:
        raise SystemExit("Use --detections-jsonl for pipeline output. RTSP/model adapters stay outside this extracted business module.")
    with Path(args.detections_jsonl).open(encoding="utf-8") as stream:
        for frame, line in enumerate(stream):
            row = json.loads(line)
            detections = row.get("detections", row.get("dets", []))
            events = monitor.update(detections, int(row.get("seq", frame)), int(row.get("timestamp_ms", time.time() * 1000)))
            for event in events:
                print(json.dumps(event, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
