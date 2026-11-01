#!/usr/bin/env bash
# Makes the screenshots of the README (docs/screenshots/*.png) from your own
# ROM sets with the host harness:
#
#   hosttest/build.sh
#   tools/screenshots.sh <romroot>      # the folder with SALAMAND and LIFEFRCE
#
# Lifeforce: the title at frame 500 and the first stage at frames 1200 and
# 3600 of a game started at frame 700. Salamander, when its set is there: the
# same frames.
set -euo pipefail
cd "$(dirname "$0")/.."
ROMROOT=${1:?usage: tools/screenshots.sh <romroot>}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
mkdir -p docs/screenshots
shot() { # <set> <frame> <name>
  python3 hosttest/ppm2png.py "$OUT/$1/frame_$(printf %05d "$2").ppm" > /dev/null
  mv "$OUT/$1/frame_$(printf %05d "$2").png" "docs/screenshots/$3.png"
}
for set in lifefrce salamand; do
  if ./hosttest/sal_host "$ROMROOT" "$set" 3601 100 "$OUT/$set" --coin 600 --start 700 --play 760 > /dev/null 2>&1; then
    shot "$set" 500 "$set-title"
    shot "$set" 1200 "$set-stage1"
    shot "$set" 3600 "$set-stage1b"
    echo "docs/screenshots/$set-*.png written"
  else
    echo "$set: no complete set under $ROMROOT, skipped"
  fi
done
