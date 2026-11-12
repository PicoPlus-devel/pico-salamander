# pico-salamander

An emulator of the arcade games Salamander and Lifeforce (Konami, 1986) for RP2350 boards with HDMI output.

# General Info

[Binaries for each configuration are at the end of this page](#downloads___).

[See the readme for the supported boards, the game data and the controls](https://github.com/PicoPlus-devel/pico-salamander#readme)

> [!IMPORTANT]
> An **RP2350** board with HSTX video output is required: the Adafruit Fruit Jam, a Pimoroni Pico Plus 2 or Raspberry Pi Pico 2, the Murmulator M2, the Adafruit Feather RP2350 with HSTX port, the Olimex RP2040-PICO-PC with a Pico 2, the Pico Computer 3 or the PCB for the Waveshare RP2350-PiZero. PSRAM is optional. The original RP2040 (Pico 1) is not supported.

# Changelog

## v0.3

- The DIP switches can now be changed in the settings menu: coins per credit, lives, difficulty, demo sounds and more. The game's own test screens are there too, as *Service mode*. Saving a change restarts the game.
- The DIP switch settings and the game chosen last are kept in `/SAVES/SALAMANDER` on the SD card. After the update, the menu that chooses the game starts on the first game once.
- The game now runs on the [Pico Computer 3](https://github.com/UKTailwind/Pico-Computer-3): HDMI, sound through HDMI or the audio jack, USB controllers, PSRAM, and NES or SNES controllers and a Wii Classic controller on the I/O header. The pin assignment may still change. See [Pico Computer 3](https://github.com/PicoPlus-devel/pico-infonesPlus#pico-computer-3) in the pico-infonesPlus readme. Thanks to @javavi.
- The game now runs on a PCB for the Waveshare RP2350-PiZero, designed by @szuping: HDMI, sound through HDMI or the audio jack, a USB controller on the PIO-USB port, NES and SNES controller ports, and a socket for an ESP32 module that connects Bluetooth controllers through [BlueRetro](https://github.com/darthcloud/blueretro). The PCB design is not yet available. See [PCB with Waveshare RP2350-PiZero](https://github.com/PicoPlus-devel/pico-infonesPlus#pcb-with-waveshare-rp2350-pizero) in the pico-infonesPlus readme.
- NES and SNES controllers on the controller ports are now read with the same timing as a real console, on all boards. Thanks to @javavi.
- Under pico-bootLoader, the settings menu entry that goes back to the bootloader is now called **Return to boot loader**.
- Fixed: on boards with HSTX video, with *Display Mode* set to DVI, the picture could randomly drop out, and now and then a single line was shown shifted to the right for one frame.

## v0.2

- Boards without PSRAM can now play, such as the Raspberry Pi Pico 2. The first time a game starts, it is copied into the flash memory of the board, which takes a few seconds. After that it also starts without the SD card.
- New setting *Disable PSRAM*, for testing this on a board that has PSRAM. Saving a change restarts the board.

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

Binaries for each configuration are listed below. Only RP2350-based boards with HSTX video output are supported.

Wiring and assembly are the same as for the NES emulator; see the [pico-infonesPlus documentation](https://github.com/PicoPlus-devel/pico-infonesPlus#setup) for the RP2350 boards listed below.

### Standalone boards

| Board | Binary |
|:--|:--|
| Adafruit Fruit Jam | [picoSalamander_AdafruitFruitJam_arm_piousb.uf2](https://github.com/PicoPlus-devel/pico-salamander/releases/latest/download/picoSalamander_AdafruitFruitJam_arm_piousb.uf2) |
| Adafruit Feather RP2350 with TLV320DAC3100 | [picoSalamander_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2](https://github.com/PicoPlus-devel/pico-salamander/releases/latest/download/picoSalamander_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2) |

### Breadboard

| Board | Binary |
|:--|:--|
| Pimoroni Pico Plus 2 or Raspberry Pi Pico 2 | [picoSalamander_AdafruitDVISD_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-salamander/releases/latest/download/picoSalamander_AdafruitDVISD_pico2_arm.uf2) |

### PicoNES PCB (PCB required)

| Board | Binary |
|:--|:--|
| Pimoroni Pico Plus 2 or Raspberry Pi Pico 2 | [picoSalamander_AdafruitDVISD_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-salamander/releases/latest/download/picoSalamander_AdafruitDVISD_pico2_arm.uf2) |

PCB: [pico_nesPCB_v2.6.zip](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/pico_nesPCB_v2.6.zip). [Readme](https://github.com/PicoPlus-devel/pico-infonesPlus#pcb-with-raspberry-pi-pico-or-pico-2-and-pimoroni-pico-plus-2)

3D-printed case: [thingiverse.com/thing:6689537](https://www.thingiverse.com/thing:6689537). When the board is fitted on male headers, use the latest top cover; the older covers assume a Pico soldered flat and leave no room for the USB cable.

### Murmulator M2

| Board | Binary |
|:--|:--|
| Murmulator M2 | [picoSalamander_MurmulatorM2_arm.uf2](https://github.com/PicoPlus-devel/pico-salamander/releases/latest/download/picoSalamander_MurmulatorM2_arm.uf2) |

### Olimex RP2040-PICO-PC

| Board | Binary |
|:--|:--|
| Olimex RP2040-PICO-PC with a Pimoroni Pico Plus 2 or Raspberry Pi Pico 2 | [picoSalamander_OlimexPicoPC_arm.uf2](https://github.com/PicoPlus-devel/pico-salamander/releases/latest/download/picoSalamander_OlimexPicoPC_arm.uf2) |

### Pico Computer 3

For more info about the Pico Computer 3 see https://github.com/UKTailwind/Pico-Computer-3

| Board | Binary |
|:--|:--|
| Pico Computer 3 (PC3) | [picoSalamander_PicoComputer3_arm.uf2](https://github.com/PicoPlus-devel/pico-salamander/releases/latest/download/picoSalamander_PicoComputer3_arm.uf2) |

### PCB Waveshare RP2350-PiZero

For more info about this PCB, designed by Liu Szuping, see [PCB with Waveshare RP2350-PiZero](https://github.com/PicoPlus-devel/pico-infonesPlus#pcb-with-waveshare-rp2350-pizero). The PCB design is not yet available.

| Board | Binary |
|:--|:--|
| Waveshare RP2350-PiZero (*) | [picoSalamander_WaveShareRP2350PiZeroLiuPCB_arm_piousb.uf2](https://github.com/PicoPlus-devel/pico-salamander/releases/latest/download/picoSalamander_WaveShareRP2350PiZeroLiuPCB_arm_piousb.uf2) |

This binary is only for the RP2350-PiZero on this PCB.

(*) If you fitted this board with PSRAM and it has a non-Winbond flash chip, apply the [flash_config fix](https://github.com/PicoPlus-devel/pico-infonesPlus#psram-with-a-non-winbond-flash-chip) before flashing the emulator.
