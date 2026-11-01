#!/usr/bin/env bash
# Takes the pictures tools/mkartwork.py lays out from your own ROM sets (the
# sets are not part of this project, so neither are pictures of the games):
#
#   tools/capture_footage.sh <romroot> <outdir>
#
# <romroot> holds the folders SALAMAND and LIFEFRCE as on the SD card. The host
# harness plays both games with the input of tools/screenshots.sh; each picture
# is a 320x240 PPM, the game at x 32-287. The frames were chosen with Salamander
# version D and Lifeforce (Japan); another version may show something else at
# the same frame. To change a part of the artwork, copy another frame over its
# name.
set -euo pipefail
cd "$(dirname "$0")/.."

[ $# -eq 2 ] || { sed -n 2,12p "$0"; exit 1; }
roms=$1
out=$2
work="$out/frames"
mkdir -p "$work"

[ -x hosttest/sal_host ] || hosttest/build.sh
run() { ./hosttest/sal_host "$roms" "$@" > /dev/null 2>&1; }
run salamand 1781 10 "$work/sal" --coin 600 --start 700 --play 760
run lifefrce 1781 10 "$work/lf" --coin 600 --start 700 --play 860

grab() { cp "$work/$1" "$out/$2.ppm"; echo "$1 -> $out/$2.ppm"; }
grab sal/frame_00560.ppm title     # theme 0 badge: the Salamander logo
grab lf/frame_00560.ppm lf-title   # theme 0 badge: the Lifeforce logo
grab sal/frame_01320.ppm left      # theme 1: the Vic Viper and formations
grab lf/frame_01770.ppm middle     # theme 1: a growth and a tentacle
grab sal/frame_01780.ppm right     # theme 1: a tentacle from the ceiling
