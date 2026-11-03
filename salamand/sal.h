/*
 * Salamander (Konami, 1986) and Lifeforce, the emulated board, for the Frens
 * family of RP2350 emulators.
 *
 * Plain C with no Pico dependencies: the same sources build for the board and
 * for the host harness in hosttest/. One set runs at a time.
 *
 * The board (Konami GX587), after MAME's konami/nemesis.cpp and nemesis_v.cpp
 * (Bryan McPhail):
 *   - a 68000 at 9.216 MHz: 512 KB of program ROM, 32 KB of work RAM, and the
 *     video RAM: two 64x32 tile maps of 8x8 characters (with per-line X and
 *     per-column Y scroll), 64 KB of character RAM that holds every character
 *     and sprite as 4-bit pixels, 256 zoomable sprites and a palette of 2048
 *     colours;
 *   - a Z80 at 3.58 MHz with a YM2151, a K007232 PCM chip with 128 KB of
 *     samples and a VLM5030 speech chip with 16 KB of speech, fed by a latch
 *     and an interrupt line.
 * The raster is 256x224 at 60.6 Hz, horizontal (ROT0).
 *
 * Video. The picture is drawn in bands of rows into the canvas of the
 * framework (sal_render_rows), from the machine's RAM as it is: the board
 * draws right after the frame pace, before the next frame of the main CPU
 * runs, which is what MAME draws at the start of vblank. A frame of the main
 * CPU (sal_run_frame) therefore ends at the start of vblank.
 *
 * Sound. The main CPU only ever talks to the sound board through the latch
 * and its interrupt line, so the sound board runs a frame after the main CPU,
 * from the frame's list of timestamped latch writes and interrupts
 * (sal_sound.c). On the board that is on core1.
 */
#ifndef SAL_CORE_H
#define SAL_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The picture. */
#define SAL_W 256
#define SAL_H 224

/* The sets: MAME's salamand with its clones, and lifefrce with its clone. */
typedef enum
{
    SAL_SET_SALAMAND = 0,
    SAL_SET_LIFEFRCE,
    SAL_SET_COUNT
} sal_set_id_t;

/* Pixel format of the canvas: RGB555 on HSTX, RGB444 on PicoDVI, RGB565 on
 * the host. */
typedef enum
{
    SAL_FMT_RGB555 = 0,
    SAL_FMT_RGB444,
    SAL_FMT_RGB565
} sal_fmt_t;

/* Inputs, active high; the bits of the board's ports IN1/IN2 for the players,
 * IN0 for the rest. */
#define SAL_IN_LEFT 0x01 /* P1 in bits 0..6, P2 the same in bits 8..14 */
#define SAL_IN_RIGHT 0x02
#define SAL_IN_UP 0x04
#define SAL_IN_DOWN 0x08
#define SAL_IN_B1 0x10 /* power-up */
#define SAL_IN_B2 0x20 /* shoot (Salamander: shoot and missile) */
#define SAL_IN_B3 0x40 /* missile (Lifeforce Japan) */
#define SAL_IN_P2(b) ((uint32_t)(b) << 8)
#define SAL_IN_START1 0x10000u
#define SAL_IN_START2 0x20000u
#define SAL_IN_COIN1 0x40000u
#define SAL_IN_COIN2 0x80000u
#define SAL_IN_SERVICE 0x100000u

/* --- File access, implemented by the board (FatFs) and the host (stdio) --- */

typedef void (*sal_dir_cb)(void *arg, const char *name, uint32_t size, int is_dir);

typedef struct
{
    void *ctx;
    /* Calls cb for every entry of dir; returns 0 when dir cannot be opened. */
    int (*list_dir)(void *ctx, const char *dir, sal_dir_cb cb, void *arg);
    void *(*open)(void *ctx, const char *path);
    uint32_t (*size)(void *ctx, void *file);
    /* Returns the number of bytes read, or -1. */
    int (*read_at)(void *ctx, void *file, uint32_t offset, void *buf, uint32_t len);
    void (*close)(void *ctx, void *file);
    /* The loader's scratch memory, all freed before it returns: the list of a
     * folder's files (6.5 KB), a 4 KB read buffer and the inflate state (about
     * 8 KB). NULL for malloc and free. */
    void *(*alloc)(void *ctx, uint32_t size);
    void (*free)(void *ctx, void *p);
} sal_io_t;

/* --- The ROM sets ----------------------------------------------------------- */

/* The regions the files are loaded into, as MAME's. */
typedef enum
{
    SAL_R_MAIN = 0, /* the 68000's ROM, 512 KB, as the CPU sees it */
    SAL_R_SOUND,    /* the Z80's ROM, 32 KB */
    SAL_R_VLM,      /* the speech ROM, 16 KB */
    SAL_R_PCM,      /* the K007232's samples, 128 KB */
    SAL_R_COUNT
} sal_region_t;

extern const uint32_t sal_region_size[SAL_R_COUNT];

typedef struct
{
    const char *name; /* MAME's name, for messages */
    uint32_t crc;
    uint32_t size;
    uint8_t region;  /* sal_region_t */
    uint8_t skip;    /* 1: contiguous; 2: every other byte (MAME's ROM_LOAD16_BYTE) */
    uint32_t offset; /* in the region */
    /* 0: every variant of the set needs the file; else the variants (bit v)
     * whose own file it is */
    uint8_t variants;
} sal_romfile_t;

#define SAL_MAX_VARIANTS 8

typedef struct
{
    const char *title;   /* "Salamander" */
    const char *folder;  /* below /roms/arcade: "SALAMAND" */
    const char *mameset; /* "salamand": the zip the set is usually found in */
    /* The variants in order of preference: MAME's set names ("salamandj"),
     * and what is shown for each ("version J"). */
    const char *variant_set[SAL_MAX_VARIANTS];
    const char *variant_name[SAL_MAX_VARIANTS];
    uint8_t nvariants;
    const sal_romfile_t *files;
    uint8_t nfiles;
    /* MAME's defaults of the DIP switches, per variant */
    uint8_t dsw0[SAL_MAX_VARIANTS], dsw1[SAL_MAX_VARIANTS];
} sal_set_info_t;

extern const sal_set_info_t sal_sets[SAL_SET_COUNT];

/* A mask of a set's files: bit i = files[i]. */
typedef uint64_t sal_files_t;

/* The files of the set's variant v (all of the set's files that variant v
 * needs). */
sal_files_t sal_variant_files(sal_set_id_t set, int v);

/* The first variant (in order of preference) whose files are all in found, or
 * -1. */
int sal_variant_found(sal_set_id_t set, sal_files_t found);

/* The variant closest to complete: for listing the missing files. */
int sal_variant_nearest(sal_set_id_t set, sal_files_t found);

/* The mask of the set's files present in dir (and dir/<mameset>, and
 * dir/<variant set> for an unzipped merged set), from the zip directories and
 * the CRCs of loose files, without loading anything. */
sal_files_t sal_probe(sal_set_id_t set, const sal_io_t *io, const char *dir);

/* The ROM loader: loads the files in `want` from dir, each to dest(arg, i)
 * (i = its index in the set's files), which may return NULL to skip one.
 * Returns the mask of the wanted files found. */
sal_files_t sal_load_files(sal_set_id_t set, sal_files_t want, const sal_io_t *io, const char *dir,
                           uint8_t *(*dest)(void *arg, int i), void *arg);

/* --- The machine's data ---------------------------------------------------
 * What the machine runs from, built from the ROMs: in memory (PSRAM on the
 * board, see sal_load) or in flash on a board without PSRAM (sal_store.c). */
typedef enum
{
    SAL_P_MAIN = 0, /* 512 KB: the 68000's ROM as 16-bit words in the host's byte order */
    SAL_P_SOUND,    /* 32 KB, as the ROM */
    SAL_P_VLM,      /* 16 KB, as the ROM */
    SAL_P_PCM,      /* 128 KB, as the ROM */
    SAL_P_COUNT
} sal_part_t;

extern const uint32_t sal_part_size[SAL_P_COUNT];

/* Loads the set's variant v from dir into memory from alloc (the parts,
 * 688 KB, plus as much again while loading; on the board PSRAM). Fills
 * parts[] and returns the mask of files found; the parts are complete only
 * when the variant's files are all in it. */
sal_files_t sal_load(sal_set_id_t set, int v, const sal_io_t *io, const char *dir, void *(*alloc)(uint32_t),
                     void (*free_fn)(void *), const uint8_t **parts);

/* --- Life cycle ------------------------------------------------------------ */

/* Once, before anything else. */
void sal_init(sal_fmt_t fmt, int samplerate);

/* Memory the core keeps for good in SRAM: the pages of the program ROM read
 * most (16 KB). NULL when it cannot be had: they are then read where they
 * are. */
typedef void *(*sal_alloc_fn)(uint32_t size);

/* Starts the machine on the parts (which must stay where they are). Returns
 * 0 when the set is not valid. */
int sal_start(sal_set_id_t set, int variant, const uint8_t *const *parts, sal_alloc_fn fast);

/* The running set, or -1. */
int sal_running(void);

/* Power-on reset. */
void sal_reset(void);

/* --- Running ---------------------------------------------------------------
 * A frame is the main CPU's sal_run_frame, then the sound board's frame from
 * the latch writes and interrupts it made: sal_sound_frame on the same core,
 * or the same function on another core with the events of sal_take_events. */

/* The 68000's cycles in a frame: 264 lines of 576. */
#define SAL_MAIN_PER_FRAME 152064

/* Runs a frame of the main CPU with these inputs (SAL_IN_*): from the start of
 * vblank to the start of the next. */
void sal_run_frame(uint32_t inputs);

/* Something the main CPU did to the sound board, at its cycle in the frame. */
typedef struct
{
    uint32_t cycle;
    uint8_t kind; /* SAL_EV_* */
    uint8_t value;
} sal_event_t;

#define SAL_EV_LATCH 0 /* the sound latch written */
#define SAL_EV_IRQ 1   /* the sound CPU's interrupt line pulsed */
/* The game writes the latch a few times a frame at most (more is dropped;
 * the host harness reports the most it saw). */
#define SAL_MAX_EVENTS 64

/* The events of the frame just run; they stay valid until the next
 * sal_run_frame. */
int sal_take_events(const sal_event_t **events);

/* The sound board: a frame from those events, n stereo samples (left and
 * right interleaved) at the rate given to sal_init. */
void sal_sound_frame(const sal_event_t *events, int nevents, int16_t *audio, int n);

/* The sound board's own reset (with sal_reset when it runs elsewhere). */
void sal_sound_reset(void);

/* --- Video ------------------------------------------------------------------ */

/* Once per frame, before its bands: the sprite list and the colours. */
void sal_render_begin(void);

/* Draws rows y0..y1-1 of the picture; `fb` is the canvas pixel of the
 * picture's (0, y0), `stride` the canvas width in pixels. */
void sal_render_rows(uint16_t *fb, int stride, int y0, int y1);

/* --- Diagnostics ------------------------------------------------------------ */

typedef struct
{
    uint32_t main_pc, sound_pc;
    uint32_t sound_idle_cycles; /* cycles the sound CPU spent halted, last frame */
    uint32_t latch_writes, sound_irqs;
    uint32_t sprites_drawn; /* last frame */
    uint32_t palette_rebuilds;
} sal_stats_t;

const sal_stats_t *sal_stats(void);

/* The core's SRAM use: the bytes from `fast` at the last sal_start. */
uint32_t sal_fast_bytes(void);

#ifdef __cplusplus
}
#endif

#endif
