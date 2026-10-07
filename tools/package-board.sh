#!/usr/bin/env bash
set -eu

if [ "$#" -ne 3 ]; then
  echo "usage: package-board.sh <runner> <model.rknn> <output-dir>" >&2
  exit 2
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUNNER="$1"
MODEL="$2"
OUTPUT="$3"

MAIN_BIN="${POSEVENT_MAIN_BIN:-}"
if [ -z "$MAIN_BIN" ]; then
  for candidate in "$ROOT/build-core/posevent" "$ROOT/build-board100/posevent" "$ROOT/build-release-check/posevent"; do
    if [ -x "$candidate" ]; then MAIN_BIN="$candidate"; break; fi
  done
fi

for required in "$RUNNER" "$MODEL" "$MAIN_BIN"; do
  if [ ! -f "$required" ] || [ ! -r "$required" ]; then
    echo "required file not found: $required" >&2
    exit 1
  fi
done

mkdir -p "$OUTPUT/bin" "$OUTPUT/configs" "$OUTPUT/models" "$OUTPUT/tools" "$OUTPUT/web" "$OUTPUT/artifacts/runtime"
cp "$MAIN_BIN" "$OUTPUT/bin/posevent"
cp "$ROOT/main.c" "$OUTPUT/main.c"
cp "$RUNNER" "$OUTPUT/bin/posevent_rknn_live"
cp "$MODEL" "$OUTPUT/models/yolopose.rknn"
cp "$ROOT/models/README.md" "$OUTPUT/models/README.md"
cp "$ROOT/region_server.py" "$OUTPUT/region_server.py"
cp "$ROOT/web/region-editor.html" "$OUTPUT/web/region-editor.html"
cp "$ROOT/configs/regions.json" "$OUTPUT/configs/regions.json"
cp "$ROOT/configs/pipeline.board-108.json" "$OUTPUT/configs/pipeline.json"
cp "$ROOT/tools/run-posevent-pipeline" "$ROOT/tools/stop-posevent-pipeline" "$ROOT/tools/posevent-watchdog" "$ROOT/tools/validate-regions.py" "$OUTPUT/tools/"
chmod +x "$OUTPUT/bin/posevent" "$OUTPUT/bin/posevent_rknn_live" "$OUTPUT/tools"/*

cat > "$OUTPUT/MANIFEST.txt" <<EOF
posevent board package
runner=posevent_rknn_live
model=models/yolopose.rknn
config=configs/pipeline.json
regions=configs/regions.json
display=wayland
EOF
if command -v shasum >/dev/null 2>&1; then
  (cd "$OUTPUT" && find bin configs models tools web -type f -print0 | sort -z | xargs -0 shasum -a 256) \
    > "$OUTPUT/SHA256SUMS"
  MODEL_SHA256="$(shasum -a 256 "$OUTPUT/models/yolopose.rknn" | awk '{print $1}')"
  printf 'model_sha256=%s\n' "$MODEL_SHA256" >> "$OUTPUT/MANIFEST.txt"
fi
echo "created board package: $OUTPUT"
