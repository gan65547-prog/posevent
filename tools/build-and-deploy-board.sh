#!/usr/bin/env bash
set -eu

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
  echo "usage: build-and-deploy-board.sh <runner> <board-host> [remote-dir]" >&2
  echo "example: tools/build-and-deploy-board.sh /path/to/posevent_rknn_live gan@192.168.0.108 /opt/posevent" >&2
  exit 2
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUNNER="$1"
BOARD="$2"
REMOTE_DIR="${3:-/opt/posevent}"

if [ ! -x "$RUNNER" ]; then
  echo "RKNN runner not executable: $RUNNER" >&2
  exit 1
fi
if [ ! -r "$ROOT/models/yolopose.rknn" ]; then
  echo "model not found: $ROOT/models/yolopose.rknn" >&2
  exit 1
fi

cmake -S "$ROOT" -B "$ROOT/build-core"
cmake --build "$ROOT/build-core" --parallel "${POSEVENT_BUILD_JOBS:-4}"
python3 "$ROOT/tools/validate-regions.py" "$ROOT/configs/regions.json"

exec "$ROOT/tools/deploy-board.sh" \
  "$RUNNER" "$ROOT/models/yolopose.rknn" "$BOARD" "$REMOTE_DIR"
