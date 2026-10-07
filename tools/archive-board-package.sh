#!/usr/bin/env bash
set -eu

if [ "$#" -ne 2 ]; then
  echo "usage: archive-board-package.sh <package-dir> <output.tar.gz>" >&2
  exit 2
fi

PACKAGE_DIR="$(cd "$1" && pwd)"
OUTPUT="$2"
mkdir -p "$(dirname "$OUTPUT")"
tar -C "$PACKAGE_DIR" -czf "$OUTPUT" .
echo "created board archive: $OUTPUT"
