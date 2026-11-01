#!/usr/bin/env bash
# Build the host harnesses:
#   hosttest/sal_host    - the Salamander core (main CPU, sound board, video, ROM loader)
#   hosttest/zex_host    - the Z80 core on the ZEXDOC/ZEXALL instruction tests
#
# The core in salamand/ has no Pico dependencies; this compiles it for Linux
# with ASan. Drop -fsanitize=address (and use -O2) for timing measurements:
#   OPT="-O2" hosttest/build.sh
# With SDL2 installed (libsdl2-dev), sal_host also gets live mode (--live,
# hosttest/host_sdl.c): a window, sound and the keyboard.
set -euo pipefail
cd "$(dirname "$0")/.."

SDL=()
if pkg-config --exists sdl2 2>/dev/null; then
  SDL=(-DHOST_SDL=1 hosttest/host_sdl.c $(pkg-config --cflags --libs sdl2))
  echo "SDL2 $(pkg-config --modversion sdl2) found: sal_host with live mode (--live)"
else
  echo "SDL2 not found: sal_host without live mode (install libsdl2-dev for --live)"
fi

OPT=${OPT:--O1 -g -fsanitize=address -fno-omit-frame-pointer}
CFLAGS=(
  $OPT -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
  -D_DEFAULT_SOURCE -DSAL_M68K_PROFILE=1
  -I salamand -I salamand/musashi -I third_party/miniz
  -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_DEFLATE_APIS
  -DMINIZ_NO_ZLIB_APIS -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES
)
# Musashi's generated code is not warning-free
MUSASHI_FLAGS=(-Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable -Wno-implicit-fallthrough)

CORE=(
  salamand/z80.c
  salamand/sal.c
  salamand/sal_sets.c
  salamand/sal_video.c
  salamand/sal_sound.c
  salamand/sal_input.c
  salamand/ym2151.c
  salamand/k007232.c
  salamand/vlm5030.c
  salamand/romload.c
  third_party/miniz/miniz.c
)
MUSASHI=(
  salamand/musashi/m68kcpu.c
  salamand/musashi/m68kops.c
)

mkdir -p hosttest/obj
for f in "${MUSASHI[@]}"; do
  gcc "${CFLAGS[@]}" "${MUSASHI_FLAGS[@]}" -c "$f" -o "hosttest/obj/$(basename "$f" .c).o"
done
gcc "${CFLAGS[@]}" hosttest/sal_host.c "${CORE[@]}" hosttest/obj/m68kcpu.o hosttest/obj/m68kops.o "${SDL[@]}" -lm -o hosttest/sal_host
gcc "${CFLAGS[@]}" -I hosttest hosttest/zex_host.c salamand/z80.c -o hosttest/zex_host
echo "built hosttest/sal_host and zex_host"
gcc "${CFLAGS[@]}" hosttest/ym_test.c salamand/ym2151.c -lm -o hosttest/ym_test
YMFM_DIR=${YMFM_DIR:-../ares/thirdparty/ymfm/src}
if [ -f "$YMFM_DIR/ymfm_opm.cpp" ]; then
  g++ -O2 -std=c++14 -I "$YMFM_DIR" hosttest/ym_compare.cpp "$YMFM_DIR/ymfm_opm.cpp" "$YMFM_DIR/ymfm_misc.cpp" "$YMFM_DIR/ymfm_ssg.cpp" -o hosttest/ym_compare
  echo "built hosttest/ym_compare (ymfm at $YMFM_DIR)"
else
  echo "no ymfm at $YMFM_DIR: ym_compare not built"
fi
