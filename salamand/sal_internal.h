/*
 * Salamander core internals: the machine state shared by the core's files.
 *
 * One machine runs at a time; its state is a single struct, `m`, for the main
 * CPU side and the video, and `snd` (sal_sound.c) for the sound board, which
 * may run on another core. The 68000's RAM is kept as 16-bit words in the
 * host's byte order (as MAME keeps it), so a word at address A is ram[A >> 1]
 * and the byte at A is ((uint8_t *)ram)[A ^ 1].
 */
#ifndef SAL_INTERNAL_H
#define SAL_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sal.h"
#include "sal_port.h"
#include "z80.h"

/* The board's timing. The main CPU: 384 x 264 pixels at 6.144 MHz, 9.216 MHz
 * = 576 cycles a line; the picture is lines 16..239. The sound CPU: 3.579545
 * MHz, 59062.5 cycles in the same frame. */
#define SAL_LINES 264
#define SAL_MAIN_PER_LINE 576
#define SAL_VBLANK_LINE 240 /* the vblank interrupt, and MAME draws the picture */
#define SAL_FIRST_LINE 16   /* the picture's first raster line */
#define SAL_SOUND_PER_FRAME 59062
#define SAL_SOUND_PER_LINE 224 /* a slice of the sound CPU, about a line */

typedef struct
{
    int set; /* sal_set_id_t, or -1 */
    int variant;
    sal_fmt_t fmt;
    int samplerate;

    /* the data, see sal_part_t; the main ROM as the 68000 bus reads it */
    const uint16_t *rom;
    const uint8_t *rom_sound, *rom_vlm, *rom_pcm;

    /* the 68000's RAM, words in the host's byte order (sal_bus.h) */
    uint16_t ram[0x4000] __attribute__((aligned(4)));      /* 080000-087fff */
    uint16_t vram[0x2000] __attribute__((aligned(4)));     /* 100000-103fff: videoram 1, 0; colorram 1, 0 */
    uint16_t charram[0x8000] __attribute__((aligned(4)));  /* 120000-12ffff */
    uint16_t spriteram[0x800] __attribute__((aligned(4))); /* 180000-180fff */
    uint16_t scroll[0x1000] __attribute__((aligned(4)));   /* 190000-191fff */
    uint8_t palette[0x1000] __attribute__((aligned(4)));   /* 090000-091fff, the low byte of each word */

    uint8_t irq1_on, flipx, flipy; /* the interrupt latch (0a0001) */
    uint8_t outlatch;              /* 0a0000: bit 3 the sound CPU's interrupt */
    uint8_t dsw0, dsw1;
    uint32_t inputs;
    int32_t frame_cycle; /* 68000 cycles of the frame done at the start of the slice being run */
    uint8_t palette_dirty;

    sal_event_t events[SAL_MAX_EVENTS];
    int nevents;

    sal_stats_t stats;
    uint32_t fast_bytes;
} sal_t;

extern sal_t m;

/* sal.c: the cycle of the frame the main CPU is at now */
uint32_t sal_main_cycle_now(void);

/* sal_video.c */
void sal_video_init(void);
void sal_video_reset(void);

/* sal_sound.c */
void sal_sound_start(void);

/* --- Profiling ---------------------------------------------------------------
 * In SAL_FRAME_STATS builds: CPU cycles summed per part of the frame into
 * sal_prof[]: 0 the main CPU, 1 the sound board (2 and 3 included), 2 the
 * YM2151, 3 the K007232 and VLM5030, 4 drawing (5 the characters and 6 the
 * sprites included). The board provides sal_cycles() and prints the sums. */
#if SAL_FRAME_STATS
#define SAL_NPROF 8
extern uint32_t sal_prof[SAL_NPROF];
uint32_t sal_cycles(void);
#define SAL_PROF_START(t) const uint32_t t = sal_cycles()
#define SAL_PROF_ADD(i, t) (sal_prof[i] += sal_cycles() - (t))
#else
#define SAL_PROF_START(t) ((void)0)
#define SAL_PROF_ADD(i, t) ((void)0)
#endif

#endif
