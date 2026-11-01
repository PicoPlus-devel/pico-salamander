# pico-salamander

An emulator of the arcade games Salamander and Lifeforce (Konami, 1986) for RP2350 boards with PSRAM and HDMI output.

# General Info

[Binaries for each configuration are at the end of this page](#downloads___).

[See the readme for the supported boards, the game data and the controls](https://github.com/PicoPlus-devel/pico-salamander#readme)

> [!IMPORTANT]
> An **RP2350** board with **PSRAM** and HSTX video output is required: the Adafruit Fruit Jam, a Pimoroni Pico Plus 2, the Murmulator M2 or the Adafruit Feather RP2350 with HSTX port. The Raspberry Pi Pico 2 has no PSRAM, and the original RP2040 (Pico 1) is not supported.

# Changelog

## v0.1

Initial release.

- Salamander and Lifeforce with sound. With both ROM sets on the card, a menu at start-up chooses the game.
- The game ROMs are not included. Copy MAME's `salamand.zip` to `/roms/arcade/SALAMAND` and `lifefrce.zip` to `/roms/arcade/LIFEFRCE` on the SD card, as they are or unzipped. The D, J and Tecfri versions of Salamander and the US and Japanese versions of Lifeforce all play. Missing files are named on screen.
- Two players can play at the same time, the second on a second controller.
- The board runs at 384 MHz.
- With a USB keyboard, Enter selects and Esc goes back in the menus.
- Can be started from pico-bootLoader, where it appears in the Arcade category.
- The release includes updateAll.exe, which installs the ROM sets on the SD card from a Windows computer. The readme explains how to use it.

<a name="downloads___"></a>
## Downloads by configuration

The ROM set installer for Windows: [updateAll.exe](https://github.com/PicoPlus-devel/pico-salamander/releases/latest/download/updateAll.exe)

Binaries for each configuration are listed below. Only RP2350-based boards with PSRAM and HSTX video output are supported.

Wiring and assembly are the same as for the NES emulator; see the [pico-infonesPlus documentation](https://github.com/PicoPlus-devel/pico-infonesPlus#setup) for the RP2350 boards listed below.

### Standalone boards

| Board | Binary |
|:--|:--|
| Adafruit Fruit Jam | [picoSalamander_AdafruitFruitJam_arm_piousb.uf2](https://github.com/PicoPlus-devel/pico-salamander/releases/latest/download/picoSalamander_AdafruitFruitJam_arm_piousb.uf2) |
| Adafruit Feather RP2350 with TLV320DAC3100 | [picoSalamander_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2](https://github.com/PicoPlus-devel/pico-salamander/releases/latest/download/picoSalamander_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2) |

### Breadboard

| Board | Binary |
|:--|:--|
| Pimoroni Pico Plus 2 | [picoSalamander_AdafruitDVISD_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-salamander/releases/latest/download/picoSalamander_AdafruitDVISD_pico2_arm.uf2) |

### PicoNES PCB (PCB required)

| Board | Binary |
|:--|:--|
| Pimoroni Pico Plus 2 | [picoSalamander_AdafruitDVISD_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-salamander/releases/latest/download/picoSalamander_AdafruitDVISD_pico2_arm.uf2) |

PCB: [pico_nesPCB_v2.6.zip](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/pico_nesPCB_v2.6.zip). [Readme](https://github.com/PicoPlus-devel/pico-infonesPlus#pcb-with-raspberry-pi-pico-or-pico-2-and-pimoroni-pico-plus-2)

3D-printed case: [thingiverse.com/thing:6689537](https://www.thingiverse.com/thing:6689537). When the board is fitted on male headers, use the latest top cover; the older covers assume a Pico soldered flat and leave no room for the USB cable.

### Murmulator M2

| Board | Binary |
|:--|:--|
| Murmulator M2 | [picoSalamander_MurmulatorM2_arm.uf2](https://github.com/PicoPlus-devel/pico-salamander/releases/latest/download/picoSalamander_MurmulatorM2_arm.uf2) |

### Olimex RP2040-PICO-PC

| Board | Binary |
|:--|:--|
| Olimex RP2040-PICO-PC with a Pimoroni Pico Plus 2 | [picoSalamander_OlimexPicoPC_arm.uf2](https://github.com/PicoPlus-devel/pico-salamander/releases/latest/download/picoSalamander_OlimexPicoPC_arm.uf2) |
