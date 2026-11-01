#!/usr/bin/env bash
# Build hosttest/sal_host_ref: the harness with an untouched upstream Musashi
# (its own jump table built at run time, bus errors and all) in place of the
# core's copy, to check the copy against it:
#
#   MUSASHI_REF=/path/to/Musashi hosttest/build_ref.sh
#   ./hosttest/sal_host <roms> lifefrce 3000 0 /tmp/a --coin 600 --start 700 --play 760 --hash a.txt
#   ./hosttest/sal_host_ref <roms> lifefrce 3000 0 /tmp/b --coin 600 --start 700 --play 760 --hash b.txt
#   cmp a.txt b.txt
#
# MUSASHI_REF is a checkout of https://github.com/kstenerud/Musashi; m68kmake
# is built and run there. The core's m68kconf.h is used for both builds.
set -euo pipefail
cd "$(dirname "$0")/.."
REF=${MUSASHI_REF:?set MUSASHI_REF to an upstream Musashi checkout}
WORK=hosttest/obj/ref
mkdir -p "$WORK"
cp "$REF"/{m68k.h,m68kcpu.c,m68kcpu.h,m68kfpu.c,m68kmmu.h,m68k_in.c,m68kmake.c} "$WORK"/
cp -r "$REF"/softfloat "$WORK"/
cp salamand/musashi/m68kconf.h "$WORK"/sal_m68kconf.h
(cd "$WORK" && gcc -O1 -o m68kmake m68kmake.c && ./m68kmake . m68k_in.c > /dev/null)
OPT=${OPT:--O1 -g -fsanitize=address -fno-omit-frame-pointer}
CFLAGS=(
  $OPT -std=c11 -w -D_DEFAULT_SOURCE -DSAL_M68K_PROFILE=1 -DSAL_HOST_REF=1 -DMUSASHI_CNF='"sal_m68kconf.h"'
  -I "$WORK" -I salamand -I salamand/musashi -I third_party/miniz
  -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_DEFLATE_APIS
  -DMINIZ_NO_ZLIB_APIS -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES
)
CORE=(salamand/z80.c salamand/sal.c salamand/sal_sets.c salamand/sal_video.c salamand/sal_sound.c
      salamand/sal_input.c salamand/ym2151.c salamand/k007232.c salamand/vlm5030.c salamand/romload.c
      third_party/miniz/miniz.c)
gcc "${CFLAGS[@]}" -c "$WORK/m68kcpu.c" -o "$WORK/m68kcpu.o"
gcc "${CFLAGS[@]}" -c "$WORK/m68kops.c" -o "$WORK/m68kops.o"
gcc "${CFLAGS[@]}" -c "$WORK/softfloat/softfloat.c" -o "$WORK/softfloat.o"
gcc "${CFLAGS[@]}" hosttest/sal_host.c "${CORE[@]}" "$WORK"/m68kcpu.o "$WORK"/m68kops.o "$WORK"/softfloat.o -lm -o hosttest/sal_host_ref
echo "built hosttest/sal_host_ref"
