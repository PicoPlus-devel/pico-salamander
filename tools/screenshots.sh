#!/usr/bin/env bash
# Makes the screenshots of the README (docs/screenshots/*.png) from your own
# ROM sets with the host harness:
#
#   hosttest/build.sh
#   tools/screenshots.sh <romroot>      # the folder with SALAMAND and LIFEFRCE
#
# Each game is run with scripted input: a coin at frame 600, the start button
# at 700 and a scripted player from frame 760 (Salamander) or 860 (Lifeforce),
# who lives long enough to reach the organic walls of the first stage. The
# title is taken at frame 560, before the coin. Salamander's tutorial is taken
# from a second run without input, in which the attract mode shows it after
# the high score table.
set -euo pipefail
cd "$(dirname "$0")/.."
ROMROOT=${1:?usage: tools/screenshots.sh <romroot>}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
mkdir -p docs/screenshots
run() { # <set> <frames> <dir> [options]
  local set=$1 frames=$2 dir=$3
  shift 3
  ./hosttest/sal_host "$ROMROOT" "$set" "$frames" 20 "$OUT/$dir" "$@" > /dev/null 2>&1
}
shot() { # <dir> <frame> <name>
  local f
  f=$(printf '%s/%s/frame_%05d' "$OUT" "$1" "$2")
  python3 hosttest/ppm2png.py "$f.ppm" > /dev/null
  mv "$f.png" "docs/screenshots/$3.png"
}
if run salamand 1761 sal --coin 600 --start 700 --play 760; then
  shot sal 560 salamand-title
  shot sal 1760 salamand-stage1
  run salamand 2101 sal-attract
  shot sal-attract 2100 salamand-tutorial
  echo "docs/screenshots/salamand-*.png written"
else
  echo "salamand: no complete set under $ROMROOT, skipped"
fi
if run lifefrce 1761 lf --coin 600 --start 700 --play 860; then
  shot lf 560 lifefrce-title
  shot lf 1460 lifefrce-stage1
  shot lf 1760 lifefrce-stage1b
  echo "docs/screenshots/lifefrce-*.png written"
else
  echo "lifefrce: no complete set under $ROMROOT, skipped"
fi
