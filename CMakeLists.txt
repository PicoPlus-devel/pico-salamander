# pico-salamander
# Salamander and Lifeforce (Konami, 1986) for RP2350 boards with HSTX video output.

cmake_minimum_required(VERSION 3.13)

set(projectname picoSalamander)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release
        CACHE STRING "Choose the type of build : None Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
endif()
message("* Current build type is : ${CMAKE_BUILD_TYPE}")

# ---------------------------------------------------------------------------
# Hardware / board selection
# ---------------------------------------------------------------------------
if(NOT HW_CONFIG)
    set(HW_CONFIG 8 CACHE STRING "Select the hardware configuration for your board")
endif()

if(NOT USE_HSTX)
    set(USE_HSTX 1 CACHE STRING "Use HSTX for display output when possible")
endif()

include("pico_shared/BoardConfigs.cmake")

if(NOT PICO_BOARD)
    set(PICO_BOARD pico2 CACHE STRING "Board type")
    message("PICO_BOARD not set, using default: ${PICO_BOARD}")
endif()

# RP2350 with HSTX video output only: the board runs at 384 MHz, which the
# PicoDVI back-end cannot (it ties the system clock to the pixel clock).
if(PICO_PLATFORM STREQUAL "rp2040")
    message(FATAL_ERROR
        "picoSalamander is RP2350-only - build with './bld.sh -2 -c <HW_CONFIG>'.")
endif()
if(GPIOHSTXD0 EQUAL 0)
    message(FATAL_ERROR
        "picoSalamander needs a board with HSTX video output (HW_CONFIG 2, 5, 8, 13, 14, 15, 17 or 18).")
endif()

add_definitions(-DNDEBUG)

# ---------------------------------------------------------------------------
# TinyUSB family support + Pico SDK init
# Include family_support.cmake BEFORE pico_sdk_init() so that TinyUSB picks up
# the board-specific configuration (PIO USB VBUSEN pin, DP pin, etc.) from the
# BOARD variable set by BoardConfigs.cmake.
# ---------------------------------------------------------------------------
set(FAMILY rp2040)
include($ENV{PICO_SDK_PATH}/lib/tinyusb/hw/bsp/family_support.cmake)

# Some boards (e.g. adafruit_metro_rp2350) force CFG_TUH_RPI_PIO_USB=1 in their
# board.cmake; strip it when PIO USB is disabled for this build.
if(ENABLE_PIO_USB EQUAL 0 AND TARGET tinyusb_common_base)
    get_target_property(_tu_defs tinyusb_common_base INTERFACE_COMPILE_DEFINITIONS)
    if(_tu_defs)
        list(REMOVE_ITEM _tu_defs "CFG_TUH_RPI_PIO_USB=1")
        set_target_properties(tinyusb_common_base PROPERTIES
            INTERFACE_COMPILE_DEFINITIONS "${_tu_defs}")
    endif()
endif()

project(${projectname} C CXX ASM)

pico_sdk_init()

if(ENABLE_PIO_USB EQUAL 1)
    message("Checking for PIO USB support...")
    check_and_add_pico_pio_usb_support()
endif()
message("PIO USB support: ${ENABLE_PIO_USB}, using PIO: ${PIO_USB_USE_PIO}")

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
# salamand/ is the emulated board, plain C with no Pico dependencies; the host
# harness in hosttest/ builds the same files. miniz provides inflate and CRC32
# for reading the ROM sets straight from MAME's zips.
# salamand/z80_ops.h is generated from salamand/z80.lst by tools/z80gen.py, and
# salamand/musashi/m68kops.c with its dispatch tables by tools/m68k_dispatch.py
# from Musashi's generated file; both are checked in, so the build needs no
# Python.
set(SAL_CORE_SRC
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
    salamand/sal_store.c
    third_party/miniz/miniz.c
)
set(SAL_M68K_SRC
    salamand/musashi/m68kcpu.c
    salamand/musashi/m68kops.c
)

add_executable(${projectname}
    main.cpp
    splash.cpp
    ${SAL_CORE_SRC}
    ${SAL_M68K_SRC}
    DefaultSS444.c
    DefaultSS555.c
)

# Only inflate and crc32 are used; everything else in miniz is compiled out.
target_compile_definitions(${projectname} PRIVATE
    MINIZ_NO_STDIO
    MINIZ_NO_TIME
    MINIZ_NO_ARCHIVE_APIS
    MINIZ_NO_DEFLATE_APIS
    MINIZ_NO_ZLIB_APIS
    MINIZ_NO_ZLIB_COMPATIBLE_NAMES
)

# The core runs every frame; build it for speed regardless of the build type.
# Its hot functions are placed in SRAM (SAL_HOT in salamand/sal_port.h), and
# -fno-tree-loop-distribute-patterns keeps GCC from turning their copy loops
# back into calls to memcpy, which lives in flash. -fno-strict-aliasing: the
# 68000 core and the RAM it reads as words and bytes (sal_bus.h), and a GCC 15
# miscompile seen in another port of Musashi at -O3.
set_source_files_properties(${SAL_CORE_SRC} ${SAL_M68K_SRC} PROPERTIES
    COMPILE_OPTIONS "-O2;-fno-tree-loop-distribute-patterns;-fno-strict-aliasing")

# EXTRA_CMAKE_ARGS=-DSAL_FRAME_STATS=1 ./bld.sh ... prints over the serial
# console, every five seconds, the time spent per part of a frame (main CPU,
# sound board, drawing) and the time left before the display needs the next
# frame (sal_prof[] in salamand/sal_internal.h; see main.cpp).
if(SAL_FRAME_STATS)
    target_compile_definitions(${projectname} PRIVATE SAL_FRAME_STATS=1)
endif()

pico_set_program_name(${projectname} "${projectname}")
pico_set_program_version(${projectname} "0.1")

# ---------------------------------------------------------------------------
# stdio routing
# ---------------------------------------------------------------------------
if(UART_ENABLED EQUAL 0)
    message("Disabling UART output for this board.")
    pico_enable_stdio_uart(${projectname} 0)
else()
    pico_enable_stdio_uart(${projectname} 1)
endif()
pico_enable_stdio_usb(${projectname} 0)

# ---------------------------------------------------------------------------
# What family_configure_target() used to do for us, inlined.
# ---------------------------------------------------------------------------
target_compile_definitions(${projectname} PRIVATE
    PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
)

target_compile_options(${projectname} PRIVATE
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-cast-function-type
    -Wno-maybe-uninitialized
)

# ---------------------------------------------------------------------------
# Board pins and features, from BoardConfigs.cmake
# ---------------------------------------------------------------------------
target_compile_definitions(${projectname} PUBLIC
    DVICONFIG=${DVICONFIG}
    SDCARD_PIN_CS=${SD_CS}
    SDCARD_PIN_SCK=${SD_SCK}
    SDCARD_PIN_MOSI=${SD_MOSI}
    SDCARD_PIN_MISO=${SD_MISO}
    SDCARD_PIO=pio1
    SDCARD_SPI=${SD_SPI}
    NES_PIN_CLK=${NES_CLK}
    NES_PIN_DATA=${NES_DATA}
    NES_PIN_LAT=${NES_LAT}
    NES_PIO=${NES_PIO}
    NES_PIN_CLK_1=${NES_CLK_1}
    NES_PIN_DATA_1=${NES_DATA_1}
    NES_PIN_LAT_1=${NES_LAT_1}
    NES_PIO_1=${NES_PIO_1}
    WII_PIN_SDA=${WII_SDA}
    WII_PIN_SCL=${WII_SCL}
    LED_GPIO_PIN=${LED_GPIO_PIN}
    HW_CONFIG=${HW_CONFIG}
    WIIPAD_I2C=${WIIPAD_I2C}
    USE_I2S_AUDIO=${USE_I2S_AUDIO}
    PICO_AUDIO_I2S_DATA_PIN=${PICO_AUDIO_I2S_DATA_PIN}
    PICO_AUDIO_I2S_CLOCK_PIN_BASE=${PICO_AUDIO_I2S_CLOCK_PIN_BASE}
    PICO_AUDIO_I2S_PIO=${PICO_AUDIO_I2S_PIO}
    PICO_AUDIO_I2S_DMA_IRQ=1
    PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED=${PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED}
    PICO_AUDIO_I2S_RESET_PIN=${PICO_AUDIO_I2S_RESET_PIN}
    PICO_AUDIO_I2S_INTERRUPT_PIN=${PICO_AUDIO_I2S_INTERRUPT_PIN}
    PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON=${PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON}
    CFG_TUH_RPI_PIO_USB=${ENABLE_PIO_USB}
    PIO_USB_USE_PIO=${PIO_USB_USE_PIO}
    PSRAM_CS_PIN=${PSRAM_CS_PIN}
    GPIOHSTXD0=${GPIOHSTXD0}
    GPIOHSTXD1=${GPIOHSTXD1}
    GPIOHSTXD2=${GPIOHSTXD2}
    GPIOHSTXCK=${GPIOHSTXCK}
    GPIOHSTXINVERTED=${GPIOHSTXINVERTED}
    ENABLE_VU_METER=${ENABLE_VU_METER}
    WIIPAD_DELAYED_START=${WIIPAD_DELAYED_START}
    ENABLEDVI=1
    FRENS_FORCE_USB_MSC_IN_SETTINGS=1   # no ROM browser: offer USB drive mode in the in-game settings
    I2S_AUDIO_RING_SIZE=2048            # room for the audio cushion and a whole frame, see pushAudio() in main.cpp
    # The flash store (main.cpp) ends where the chip itself says it does
    # (storage_get_flash_capacity()); the board header may set less, e.g. 4 MB
    # for pico2. The SDK's flash_range_erase/program hard_assert against this,
    # so it is the most XIP can reach. Spelled as BootPartition.cmake does.
    PICO_FLASH_SIZE_BYTES=0x1000000
)

# ---------------------------------------------------------------------------
# USB drive mode: the "USB drive mode" entry in the settings menu shows the SD
# card on a PC as a mass storage device, which is how the ROM set gets onto the
# card without taking it out of the board.
# ---------------------------------------------------------------------------
option(ENABLE_USB_MSC "Show the SD card on a PC over USB from the settings menu" ON)
if(ENABLE_USB_MSC)
    set(USB_MSC_ENABLED 1)
else()
    set(USB_MSC_ENABLED 0)
endif()
message(STATUS "USB drive mode (mass storage): ${USB_MSC_ENABLED}")
target_compile_definitions(${projectname} PRIVATE FRENS_USB_MSC=${USB_MSC_ENABLED})
if(USB_MSC_ENABLED)
    target_link_libraries(${projectname} PRIVATE tinyusb_device pico_unique_id)
    # Widen the pool of claimable hardware spinlocks from 8 to 12: on PicoDVI
    # builds dvi::DVI and the TinyUSB host stack use most of the default eight,
    # and bringing the device stack up for USB drive mode would run out.
    target_compile_definitions(${projectname} PRIVATE
        PICO_SPINLOCK_ID_STRIPED_FIRST=16
        PICO_SPINLOCK_ID_STRIPED_LAST=19
        PICO_SPINLOCK_ID_CLAIM_FREE_FIRST=20
        PICO_SPINLOCK_ID_CLAIM_FREE_LAST=31
    )
endif()

target_compile_definitions(${projectname} PRIVATE
    PICO_PIO_USE_GPIO_BASE=1
)
# As in pico-outrun: HSTX_CORE1_STACK_IN_SCRATCH_X is deliberately not set, so
# core1's stack is not placed directly below core0's, and core0 gets the whole
# SCRATCH_Y bank.
target_compile_definitions(${projectname} PUBLIC
    PICO_CORE1_STACK_SIZE=4096
)
target_compile_definitions(${projectname} PRIVATE
    PICO_STACK_SIZE=4096
)

if(DEFINED PICO_DEFAULT_UART_TX_PIN)
    message(STATUS "Using PICO_DEFAULT_UART_TX_PIN: ${PICO_DEFAULT_UART_TX_PIN}")
    target_compile_definitions(${projectname} PRIVATE
        PICO_DEFAULT_UART_TX_PIN=${PICO_DEFAULT_UART_TX_PIN}
    )
endif()
if(DEFINED PICO_DEFAULT_UART_RX_PIN)
    message(STATUS "Using PICO_DEFAULT_UART_RX_PIN: ${PICO_DEFAULT_UART_RX_PIN}")
    target_compile_definitions(${projectname} PRIVATE
        PICO_DEFAULT_UART_RX_PIN=${PICO_DEFAULT_UART_RX_PIN}
    )
endif()
if(DEFINED PICO_DEFAULT_UART)
    message(STATUS "Using PICO_DEFAULT_UART: ${PICO_DEFAULT_UART}")
    target_compile_definitions(${projectname} PRIVATE
        PICO_DEFAULT_UART=${PICO_DEFAULT_UART}
    )
endif()

# ---------------------------------------------------------------------------
# PIO USB (optional)
# ---------------------------------------------------------------------------
if(ENABLE_PIO_USB EQUAL 1)
    if(NOT PIO_DP_PLUS_PIN EQUAL -1)
        message(STATUS "Using PIO USB DP pin: ${PIO_DP_PLUS_PIN}")
        target_compile_definitions(${projectname} PRIVATE
            PICO_DEFAULT_PIO_USB_DP_PIN=${PIO_DP_PLUS_PIN}
        )
    endif()
    message("Adding target link libraries for PIO USB support.")
    target_link_libraries(${projectname} PRIVATE tinyusb_pico_pio_usb)
endif()

# ---------------------------------------------------------------------------
# Link
# ---------------------------------------------------------------------------
target_link_libraries(${projectname} PRIVATE
    pico_stdlib
    pico_multicore
    hardware_dma
    hardware_pio
    hardware_i2c
    hardware_interp
    hardware_timer
    hardware_clocks
    hardware_pwm
    hardware_flash
    dvi
    util
    tinyusb_host
    tinyusb_board
    pico_shared
    xinput_host
)

if(PICO_BOARD STREQUAL "pico_w" OR PICO_BOARD STREQUAL "pico2_w")
    target_link_libraries(${projectname} PRIVATE pico_cyw43_arch_none)
endif()

target_include_directories(${projectname} PRIVATE
    pico_lib
    pico_shared
    tusb_xinput
    salamand
    salamand/musashi
    third_party/miniz
    .
)

add_subdirectory(pico_lib/dvi)
add_subdirectory(pico_lib/util)
add_subdirectory(pico_shared)
add_subdirectory(tusb_xinput xinput_host)

target_link_options(${projectname} PRIVATE -Xlinker --print-memory-usage --data-sections)

# Optional: relink to run from the bootloader's application partition (for use
# with pico-bootLoader). Off by default so standalone builds are unchanged.
# bld.sh -b sets -DBUILD_FOR_BOOTLOADER=ON.
if(BUILD_FOR_BOOTLOADER)
    include("pico_shared/BootPartition.cmake")
    frens_offset_for_bootloader(${projectname})
endif()

pico_add_extra_outputs(${projectname})
