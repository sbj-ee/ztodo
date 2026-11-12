#!/usr/bin/env bash
# Render assets/ztodo.svg to assets/icons/ztodo-<size>.png (needs rsvg-convert:
# `sudo apt install librsvg2-bin` / `brew install librsvg`). The PNGs are
# committed, so builds don't need this; run it after changing the SVG.
set -euo pipefail
cd "$(dirname "$0")/.."
for s in 16 24 32 48 64 128 256 512 1024; do
  rsvg-convert -w "$s" -h "$s" assets/ztodo.svg -o "assets/icons/ztodo-$s.png"
done
echo "rendered: $(ls assets/icons | tr '\n' ' ')"
