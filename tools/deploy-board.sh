#!/usr/bin/env bash
set -eu

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
  echo "usage: deploy-board.sh <runner> <model.rknn> <board-host> [remote-dir]" >&2
  exit 2
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUNNER="$1"
MODEL="$2"
BOARD="$3"
REMOTE_DIR="${4:-/opt/posevent}"
BOARD_DISPLAY_MODE="${POSEVENT_BOARD_DISPLAY_MODE:-wayland}"
BOARD_FRAMEBUFFER="${POSEVENT_BOARD_FRAMEBUFFER:-/dev/fb0}"
PACKAGE="$(mktemp -d "${TMPDIR:-/tmp}/posevent-board.XXXXXX")"
trap 'rm -rf "$PACKAGE"' EXIT

"$ROOT/tools/package-board.sh" "$RUNNER" "$MODEL" "$PACKAGE"

echo "uploading package to ${BOARD}:${REMOTE_DIR}"
ssh "$BOARD" "
  mkdir -p '$REMOTE_DIR/artifacts/runtime'
  if [ -f '$REMOTE_DIR/configs/regions.json' ]; then
    cp '$REMOTE_DIR/configs/regions.json' '$REMOTE_DIR/artifacts/runtime/regions.before-deploy.json'
  fi
"
# Prevent macOS extended attributes from being copied into the Linux package.
COPYFILE_DISABLE=1 tar -C "$PACKAGE" -cf - . | ssh "$BOARD" "tar -C '$REMOTE_DIR' -xf -"

ssh "$BOARD" "
  set -eu
  cd '$REMOTE_DIR'
  if [ -d /opt/mlfw-dev/mlfw/lib ]; then
    export LD_LIBRARY_PATH=/opt/mlfw-dev/mlfw/lib:\${LD_LIBRARY_PATH:-}
  fi
  mkdir -p artifacts/runtime
  cc -std=c11 -Wall -Wextra -Wpedantic main.c -o bin/posevent
  chmod +x bin/posevent
  if [ -f artifacts/runtime/regions.before-deploy.json ]; then
    cp artifacts/runtime/regions.before-deploy.json configs/regions.json
  fi
  # The board-native launcher and preserved regions are runtime artifacts;
  # refresh the deployed checksum list after those intentional substitutions.
  if command -v sha256sum >/dev/null 2>&1; then
    find bin configs models tools web -type f -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
  fi
  if [ -x tools/stop-posevent-pipeline ]; then tools/stop-posevent-pipeline || true; fi
  if [ -f artifacts/runtime/web.pid ]; then
    kill \"\$(cat artifacts/runtime/web.pid)\" 2>/dev/null || true
    rm -f artifacts/runtime/web.pid
  fi
  POSEVENT_REGION_SERVER='$REMOTE_DIR/region_server.py' \
    nohup '$REMOTE_DIR/bin/posevent' web \
      --host 0.0.0.0 --port 9191 \
      --source rtsp --url 'rtsp://192.168.0.108:8554/vislbox-business' \
      --regions '$REMOTE_DIR/configs/regions.json' \
      >'$REMOTE_DIR/artifacts/runtime/web.log' 2>&1 &
  echo \$! >'$REMOTE_DIR/artifacts/runtime/web.pid'
  POSEVENT_ENTRYPOINT='$REMOTE_DIR/bin/posevent' \
    POSEVENT_PIPELINE_BIN='$REMOTE_DIR/bin/posevent_rknn_live' \
    POSEVENT_PIPELINE_CONFIG='$REMOTE_DIR/configs/pipeline.json' \
    POSEVENT_INPUT_URL='rtsp://192.168.0.108:8554/vislbox-business' \
    POSEVENT_MODEL_PATH='$REMOTE_DIR/models/yolopose.rknn' \
    POSEVENT_REGIONS_CONFIG='$REMOTE_DIR/configs/regions.json' \
    POSEVENT_DISPLAY_MODE='$BOARD_DISPLAY_MODE' \
    POSEVENT_FRAMEBUFFER='$BOARD_FRAMEBUFFER' \
    POSEVENT_MAX_FRAMES=0 \
    POSEVENT_STATE_DIR='$REMOTE_DIR/artifacts/runtime' \
    tools/run-posevent-pipeline
  sleep 2
  curl -fsS http://127.0.0.1:9191/regions.json >/dev/null
  kill -0 \"\$(cat artifacts/runtime/pipeline.pid)\"
  echo deployment_health=PASS
"

echo "deployment complete: ${BOARD}:${REMOTE_DIR}"
