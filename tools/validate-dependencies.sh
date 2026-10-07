#!/usr/bin/env bash
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$root/artifacts/dependency-scan.txt}"
mkdir -p "$(dirname "$out")"
{
  echo "posevent dependency scan"
  if rg -n -i '\bclib\b|\bsic\b|\bmlfw\b|\bvislbox\b|\bedge_sdk\b' "$root" --glob '!docs/**' --glob '!build-core/**' --glob '!__pycache__/**' --glob '!artifacts/**'; then exit 1; else echo "forbidden source references: none"; fi
  if find "$root" -path "$root/build-core" -prune -o -path "$root/build" -prune -o -path "$root/artifacts" -prune -o -type f \( -name '*.so' -o -name '*.a' -o -name '*.dylib' \) -print -quit | grep -q .; then echo "bundled native libraries found"; exit 1; else echo "bundled native libraries: none"; fi
} >"$out"
cat "$out"
