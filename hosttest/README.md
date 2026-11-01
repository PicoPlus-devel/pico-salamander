# Host test harness

The emulated board in `salamand/` is plain C without Pico dependencies, and also builds for Linux. The commands below are run from the root of the repository:

```sh
hosttest/build.sh
./hosttest/sal_host ~/roms/arcade lifefrce 3600 300 hosttest/out --coin 600 --start 700 --play 760 --wav hosttest/out/game.wav
python3 hosttest/ppm2png.py hosttest/out/frame_02400.ppm
./hosttest/ym_test
./hosttest/zex_host zexdoc.com zexall.com
```

`sal_host` reads the sets from a folder laid out as the SD card (`SALAMAND`, `LIFEFRCE`) through the same loader as the firmware, runs the given number of frames with scripted input, writes every Nth frame as a 320 x 240 PPM and the sound as a stereo WAV file. Every second it reports the time per frame of the main CPU, the sound board and the drawing, and what the machine did (the program counters, the sound latch writes and interrupts, the sprites drawn). `--layers` draws only the characters or only the sprites, `--dumpram` writes the video RAM of a frame to files, and `--only` plays one sound chip at a time.

`ym_test` keys one note on the YM2151 port and prints the output's level and pitch; `zex_host` runs the ZEXDOC and ZEXALL instruction exercisers on the Z80 core (the test programs are not part of this repository).

## Sound compared with ymfm

The YM2151 can be checked against ymfm, the OPM emulation MAME uses today. `hosttest/build.sh` builds `ym_compare` when it finds ymfm's sources (`YMFM_DIR`, by default the copy in ares next to this repository):

```sh
./hosttest/sal_host ~/roms/arcade lifefrce 4200 0 hosttest/out --ymlog a.log --only ym --wav a.wav
./hosttest/ym_compare a.log b.wav
python3 hosttest/wavcompare.py a-mono.wav b.wav     # a.wav made mono first, e.g. with sox or ffmpeg
```

`--ymlog` records every write to the YM2151, `ym_compare` plays them through ymfm at the chip's own rate and brings the result to 44.1 kHz, and `wavcompare.py` compares the two recordings: their level, the strongest frequency and the spectrum over windows of 50 ms.

## Live mode

With SDL2 installed (`libsdl2-dev`), `--live` plays the game in a window with sound, in real time:

```sh
./hosttest/sal_host ~/roms/arcade salamand --live
./hosttest/sal_host ~/roms/arcade lifefrce --live --scale 4
```

The keys are those of a USB keyboard on the board, mapped as on the board: the arrow keys move, X shoots, Z is the power-up, C or V the missile (Lifeforce Japan), A inserts a coin when released and S is the start button; 5, 1 and 2 work as in MAME (coin, 1 player start, 2 player start). A + S resets the game. Space pauses, N steps one frame while paused, Tab (held) runs fast, F12 saves the frame on screen and Esc quits.

## Profiling the main CPU

`--profile FILE` counts the main CPU's instructions per 4 KB page of its ROM and per opcode handler of the 68000 core, and writes them to `FILE.pages` and `FILE.handlers`, most first. The pages are what `hot_pages` in `salamand/sal.c` copies into SRAM on the board; the handlers, in `salamand/musashi/hot_handlers.txt`, are placed in SRAM by `tools/m68k_dispatch.py --hot` when `salamand/musashi/m68kops.c` is regenerated from Musashi's output:

```sh
./hosttest/sal_host ~/roms/arcade lifefrce 4200 0 hosttest/out --profile hosttest/out/prof
cd /path/to/Musashi && gcc -o m68kmake m68kmake.c && ./m68kmake . m68k_in.c && cd -
tools/m68k_dispatch.py /path/to/Musashi/m68kops.c salamand/musashi --hot salamand/musashi/hot_handlers.txt
```

## The 68000 core against Musashi

The core's Musashi has its jump table replaced by tables made ahead of time, and the bus error state, the FPU and the MMU left out. `build_ref.sh` builds the harness with an untouched upstream Musashi instead, and `--hash` writes a CRC of the main CPU's registers and RAM per frame; the two must agree over a run with the same input:

```sh
MUSASHI_REF=/path/to/Musashi hosttest/build_ref.sh
./hosttest/sal_host     ~/roms/arcade lifefrce 3000 0 hosttest/out/a --coin 600 --start 700 --play 760 --hash a.txt
./hosttest/sal_host_ref ~/roms/arcade lifefrce 3000 0 hosttest/out/b --coin 600 --start 700 --play 760 --hash b.txt
cmp a.txt b.txt
```
