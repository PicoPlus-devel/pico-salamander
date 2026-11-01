# pico-salamander

**pico-salamander** is an emulator of the arcade games **Salamander** and **Lifeforce** (Konami, 1986) for RP2350-based microcontroller boards with PSRAM, with video and audio over HDMI. It emulates the original board (Konami GX587): the 68000 main CPU, its two scrolling layers of characters, its zoomable sprites and its palette, and the sound board with its Z80, the Yamaha YM2151 FM chip, the Konami K007232 PCM chip and the Sanyo VLM5030 speech chip. The emulation is based on the nemesis driver of [MAME](https://www.mamedev.org/), the 68000 core [Musashi](https://github.com/kstenerud/Musashi) and MAME's Z80, YM2151, K007232 and VLM5030 emulations.

It uses the same menu, display, audio and controller framework as this family of emulators:

- NES: [pico-infonesPlus](https://github.com/PicoPlus-devel/pico-infonesPlus)
- Super Nintendo: [pico-snesPlus](https://github.com/PicoPlus-devel/pico-snesPlus)
- Sega Master System / Game Gear: [pico-smsplus](https://github.com/PicoPlus-devel/pico-smsplus)
- Game Boy / Game Boy Color: [pico-peanutGB](https://github.com/PicoPlus-devel/pico-peanutGB)
- Sega Mega Drive / Genesis: [pico-genesisPlus](https://github.com/PicoPlus-devel/pico-genesisPlus)
- OutRun: [pico-outrun](https://github.com/PicoPlus-devel/pico-outrun)
- 1943 and 1943 Kai: [pico-1943](https://github.com/PicoPlus-devel/pico-1943)
- Phoenix: [pico-phoenix](https://github.com/PicoPlus-devel/pico-phoenix)
- Moon Cresta: [pico-mooncresta](https://github.com/PicoPlus-devel/pico-mooncresta)
- Pac-Man, Galaga, 1942 and other games: [pico-galagino](https://github.com/PicoPlus-devel/pico-galagino)

**The ROM sets are not included and must be supplied by the user.** They are copyright Konami and are not distributed with this project.

***

## Screenshots

Lifeforce (Japan) as the emulator draws it.

<table>
  <tr>
    <td><img width="320" alt="Title screen of Lifeforce" src="docs/screenshots/lifefrce-title.png" /></td>
    <td><img width="320" alt="The first stage of Lifeforce" src="docs/screenshots/lifefrce-stage1.png" /></td>
  </tr>
  <tr>
    <td align="center">Lifeforce: title screen</td>
    <td align="center">Lifeforce: the first stage</td>
  </tr>
</table>

***

## Game data

Two MAME sets are supported, each in its own folder on the SD card:

| Game | MAME set | SD card folder |
| --- | --- | --- |
| Salamander | `salamand`, or one of its clones `salamandj`, `salamandt` | `/roms/arcade/SALAMAND` |
| Lifeforce | `lifefrce` or `lifefrcej` | `/roms/arcade/LIFEFRCE` |

The versions of Salamander differ in the two program ROMs of the main CPU (and, for the Tecfri licence, in the sound ROM and the way the data is split over ROMs). The first complete one is used, in this order: `salamand` (version D), `salamandj` (version J), `salamandt` (Tecfri licence). Lifeforce is tried as `lifefrce` (US) first, then `lifefrcej` (Japan), the version with three buttons. A set takes about 690 KB.

Copy the sets to their folders in either form:

- **The zip as it is**: `salamand.zip` and `lifefrce.zip`. Merged, split and non-merged sets all work. In MAME's merged `salamand.zip` the clones are in subfolders of the archive; they are found there. In MAME, Lifeforce is a clone of Salamander: a split `lifefrce.zip` holds only the files that differ and needs `salamand.zip` in the same folder for the rest, and MAME's merged `salamand.zip` holds Lifeforce as well, so it can be copied to both folders.
- **The files, unzipped.** Subfolders named after the set or its clones (`salamand`, `salamandj`, ...), which unzipping commonly produces, are searched as well.

Files are recognised by their size and CRC32, so their names do not matter, and a corrupt or different file is never loaded. The board creates the two folders on first start.

When both games are on the card, a menu at start-up chooses one; it starts the game played last after 10 seconds without input. *Quit game* in the settings menu returns to it.

When no set is complete, the board shows a screen that names the missing files. **SELECT + START** opens the settings menu from that screen; its *USB drive mode* shows the SD card on a computer, so the ROM sets can be copied to it without removing the card. After leaving the menu, the board restarts and looks for the sets again.

The game's data is built in PSRAM each time a game starts. A board without PSRAM shows a screen saying so; see [Supported hardware](#supported-hardware).

***

## Supported hardware

pico-salamander runs on the RP2350 configurations of this family that have HSTX video output and PSRAM. RP2040 boards, boards with PicoDVI output and boards without PSRAM are not supported.

| HW_CONFIG | Hardware | Binary |
| --- | --- | --- |
| 2 | Pimoroni Pico Plus 2 with the Adafruit DVI Breakout and microSD breakout, or on the PicoNES PCB | `picoSalamander_AdafruitDVISD_pico2_arm.uf2` |
| 8 | Adafruit Fruit Jam | `picoSalamander_AdafruitFruitJam_arm_piousb.uf2` |
| 13 | Murmulator M2 | `picoSalamander_MurmulatorM2_arm.uf2` |
| 14 | Adafruit Feather RP2350 with HSTX Port and TLV320DAC3100 I2S DAC | `picoSalamander_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2` |
| 15 | Olimex RP2040-PICO-PC with a Pimoroni Pico Plus 2 | `picoSalamander_OlimexPicoPC_arm.uf2` |

A Raspberry Pi Pico 2 has no PSRAM: in configurations 2 and 15 a Pimoroni Pico Plus 2 (8 MB of PSRAM) is required. For wiring and assembly instructions, see the setup sections of the [pico-infonesPlus README](https://github.com/PicoPlus-devel/pico-infonesPlus#setup). To flash a board, hold BOOTSEL while connecting it over USB, then copy the `.uf2` file onto the USB drive that appears.

The board runs at 384 MHz (core voltage 1.50 V), as pico-outrun does. The main CPU and the drawing run on the first core; the second core feeds the display and runs the sound board (the Z80 and the three sound chips) in between. When a frame takes longer than the display allows, the next picture is not drawn, so the game and its sound keep their speed.

Audio is sent over HDMI. Boards with an I2S DAC (configurations 8, 13 and 14) can use it instead through the *External Audio* setting; on the Fruit Jam, plugging in headphones selects it automatically. Configuration 15 also plays the sound on its PWM audio jack.

***

## Controls

Salamander has an eight-way joystick and two buttons for each player (power-up, and shoot, which fires the missiles as well); Lifeforce (Japan) has a third button for the missiles. Two players play at the same time.

| Controller | Salamander / Lifeforce |
| --- | --- |
| D-pad | Move the ship |
| A | Shoot (Salamander: shoot and missile) |
| B | Power-up |
| X or Y | Missile (Lifeforce Japan) |
| SELECT | Insert a coin (on release) |
| START | 1 player start |
| SELECT on the second controller | Insert a coin in the second slot (on release) |
| START on the second controller | 2 player start |
| START + A | Frame rate display on/off |
| SELECT + START | Settings menu |

The first controller plays player 1 and the second controller player 2: the second USB controller, or the controller on the second GPIO port. USB game controllers, NES and SNES controllers on the GPIO ports and the Wii Classic controller (player 1) are supported, as in the other emulators of this family. A has a rapid fire setting. A coin is inserted when SELECT is released, and only when no other button was pressed while it was held, so opening the settings menu does not insert a coin; each controller's SELECT feeds its own coin slot, so a two player game is started with a credit from each controller (or two from the first) and START on the second controller. While START is held, A switches the frame rate display (top left, in the border) on or off instead of shooting; the same setting is also in the settings menu.

The DIP switches are at MAME's defaults: 1 coin for 1 credit, 3 lives, normal difficulty, demo sounds on, upright cabinet.

***

## Settings menu

**SELECT + START** opens the settings menu during the game. It offers the screen mode with or without scanlines, the FPS overlay, audio on/off, rapid fire on A, the external audio output, the Fruit Jam volume and VU meter, *Reset Game*, the controller test, BOOTSEL mode and USB drive mode. With both games on the card it also offers *Quit game*, which returns to the menu that chooses the game. When started from pico-bootLoader, it also offers *Return to emulator selection menu*.

The settings are stored on the SD card in `/settings_ARC.dat`, a file shared by all arcade games of this family. The game has no save states, and high scores are not kept after a reset or power cycle. The game chosen last is kept in `/roms/arcade/SALAMAND/LASTSET.TXT`.

***

## Building from source

Requirements: the Raspberry Pi Pico SDK (`PICO_SDK_PATH`), Pico-PIO-USB (`PICO_PIO_USB_PATH`) for the configurations that use it, the `arm-none-eabi` toolchain and `picotool`.

```sh
git clone --recursive https://github.com/PicoPlus-devel/pico-salamander.git
cd pico-salamander
./bld.sh -c 8 -2        # one configuration: HW_CONFIG 8 (Fruit Jam)
./bld.sh -b -c 8 -2     # the same, built for pico-bootLoader (releases_bl/)
./buildAll.sh           # every supported configuration (releases/)
./bld.sh -h             # all options
```

Every configuration must be built with `-2` (RP2350). `EXTRA_CMAKE_ARGS=-DSAL_FRAME_STATS=1` prints over the serial console, every five seconds, the time a frame takes per part (main CPU, sound board, YM2151, drawing) and the time left before the display needs the next frame.

Two generated files are checked in, so the build does not need Python. `salamand/z80_ops.h`, the instruction loop of the Z80 core, comes from MAME's opcode list `salamand/z80.lst` through `tools/z80gen.py`. `salamand/musashi/m68kops.c` holds Musashi's 68000 opcode handlers with the dispatch tables of `tools/m68k_dispatch.py` in place of the jump table Musashi builds at run time, which would take 320 KB of RAM; the script also marks the handlers the game runs most (`salamand/musashi/hot_handlers.txt`, from a profile made by the host harness) for placement in SRAM. The built-in screensaver image (`DefaultSS444.c`, `DefaultSS555.c`) is made from a frame of the game by `tools/mkdefaultss.py`; it takes the picture from your own ROM set through the host harness.

### Host test harness

The emulated board in `salamand/` is plain C without Pico dependencies, and also builds for Linux. The host test harness in `hosttest/` runs both games on a PC, writes frames and sound to files or plays them live in a window, profiles the main CPU and checks the 68000 core against an unmodified Musashi. See [hosttest/README.md](hosttest/README.md).

***

## Credits and licence

- Salamander driver and video emulation: the MAME project, by Bryan McPhail. K007232 emulation: Nicola Salmoria and Hiromitsu Shioya; VLM5030 emulation: Tatsuyuki Satoh; Z80 CPU core: Juergen Buchmueller and Andrei I. Holub, by way of pico-mooncresta, pico-galagino and pico-1943. These parts are under the BSD-3-Clause licence.
- 68000 CPU core: Musashi, by Karl Stenerud; MIT licence.
- YM2151 emulation: MAME's `ym2151.cpp` (MAME 0.229), by Jarek Burczynski and Ernesto Corvi; GPL-2.0-or-later.
- See [salamand/LICENSE](salamand/LICENSE) for which file comes from where.
- The ROM loader, the drawing in bands, the sound board a frame behind on the second core and the host harness follow pico-1943 and pico-galagino.
- The research of [Liu Szuping](https://github.com/szuping) for an earlier RP2350 port of Salamander (hardware notes, timing measurements and a profile of the game) served as a reference.
- miniz (inflate and CRC32): Rich Geldreich and others; see the licence text in [third_party/miniz/miniz.c](third_party/miniz/miniz.c).

The project is distributed under the GNU General Public License v3.0; see [LICENSE](LICENSE).

Salamander and Lifeforce are copyright 1986 Konami. This project is not affiliated with Konami or with the MAME project.
