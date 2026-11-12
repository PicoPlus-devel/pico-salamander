/*
 * Salamander: the main CPU's side of the board, after MAME's
 * konami/nemesis.cpp (license BSD-3-Clause, copyright-holders Bryan McPhail).
 *
 * Main CPU (68000, 9.216 MHz):
 *   000000-07ffff  ROM
 *   080000-087fff  work RAM
 *   090000-091fff  palette: 2048 colours of xBGR555, a byte on the low half
 *                  of each word
 *   0a0000  (w)    out latch, the high byte: b1-2 coin lockouts, b3 the sound
 *                  CPU's interrupt (on its rising edge)
 *   0a0001  (w)    interrupt latch, the low byte: b0 IRQ1 enable (clearing
 *                  it drops a pending IRQ1), b1 IRQ2 enable (not wired), b2-3
 *                  flip screen
 *   0c0001  (w)    sound latch
 *   0c0002  (r)    DSW0 (coinage)
 *   0c0004  (w)    watchdog
 *   0c2000  (r)    IN0: b0-1 coins, b2 service, b3-4 starts, b5-7 DIPs
 *   0c2002  (r)    IN1: player 1, b7 cabinet
 *   0c2004  (r)    IN2: player 2
 *   0c2006  (r)    DSW1
 *   100000-103fff  video RAM: the tile codes of layer 1, then 0; the colours
 *                  of layer 1, then 0 (64x32 words each)
 *   120000-12ffff  character RAM: 4-bit pixels
 *   180000-180fff  sprites: 256 of 8 words
 *   190000-191fff  scroll RAM: X per raster line of layer 1 (256 words, then
 *                  256 words with bit 8), then layer 0; at 0f00 Y per column
 *                  of layer 0 (64 words), at 0f80 layer 1
 * Inputs are active high. IRQ1 is asserted at the start of vblank (line 240
 * of 264) when enabled, and stays asserted until the game clears the enable.
 *
 * A frame here runs from the start of vblank to the start of the next: the
 * interrupt, 24 lines of vblank, then the 240 lines down to the next vblank.
 * The picture is then what MAME draws, in its vblank callback.
 */
#include "sal_internal.h"

#include "sal_bus.h"
#include "m68k.h"

#include <stdlib.h>

sal_t m = {.set = -1};

const uint16_t *sal_fetch_page[256];

/* what is not mapped reads as zeros (flash) */
static const uint16_t zero_page[2048];

/* The 4 KB pages of the program ROM kept in SRAM, in order of how much they
 * are read: Lifeforce (Japan) runs 82% of its instructions from the first
 * three (Liu's profile of the game; the host harness's --profile measures
 * it for any set). */
static const uint8_t hot_pages[] = {0, 22, 1, 2};
#define NHOT (int)(sizeof(hot_pages) / sizeof(hot_pages[0]))

#if SAL_FRAME_STATS
uint32_t sal_prof[SAL_NPROF];
#endif

/* ---------------------------------------------------------------------------
 * The slow side of the bus
 * ------------------------------------------------------------------------ */

static void SAL_HOT(push_event)(uint8_t kind, uint8_t value)
{
    if (m.nevents >= SAL_MAX_EVENTS)
        return;
    sal_event_t *e = &m.events[m.nevents++];
    e->cycle = sal_main_cycle_now();
    e->kind = kind;
    e->value = value;
}

uint32_t SAL_HOT(sal_main_cycle_now)(void)
{
    int32_t cycle = m.frame_cycle + m68k_cycles_run();
    if (cycle < 0)
        cycle = 0;
    else if (cycle >= SAL_MAIN_PER_FRAME)
        cycle = SAL_MAIN_PER_FRAME - 1;
    return (uint32_t)cycle;
}

/* MAME's salamand_intlatch_w */
static void SAL_HOT(intlatch_w)(unsigned v)
{
    m.irq1_on = v & 1;
    if (!m.irq1_on)
        m68k_set_irq(0); /* irq1_enable_w: clears the line */
    m.flipx = (v >> 2) & 1;
    m.flipy = (v >> 3) & 1;
}

/* MAME's outlatch_w: the sound CPU's interrupt on the rising edge of bit 3
 * (sound_irq_w) */
static void SAL_HOT(outlatch_w)(unsigned v)
{
    if ((v & 8) && !(m.outlatch & 8))
    {
        push_event(SAL_EV_IRQ, 0);
        m.stats.sound_irqs++;
    }
    m.outlatch = (uint8_t)v;
}

static void SAL_HOT(soundlatch_w)(unsigned v)
{
    push_event(SAL_EV_LATCH, (uint8_t)v);
    m.stats.latch_writes++;
}

static inline unsigned SAL_HOT(port_player)(uint32_t in)
{
    return in & 0x7f; /* b7: cabinet, upright */
}

unsigned SAL_HOT(sal_m68k_read16_slow)(unsigned a)
{
    switch (a >> 16)
    {
    case 0x09:
        return m.palette[(a & 0x1fff) >> 1];
    case 0x0c:
        switch (a & 0xffff)
        {
        case 0x0002:
            return m.dsw0;
        case 0x2000:
        {
            /* b5 flip screen (DIP), b6 unused, b7 service (DIP): active low */
            unsigned v = m.in0_dips;
            if (m.inputs & SAL_IN_COIN1)
                v |= 0x01;
            if (m.inputs & SAL_IN_COIN2)
                v |= 0x02;
            if (m.inputs & SAL_IN_SERVICE)
                v |= 0x04;
            if (m.inputs & SAL_IN_START1)
                v |= 0x08;
            if (m.inputs & SAL_IN_START2)
                v |= 0x10;
            return v;
        }
        case 0x2002:
            return port_player(m.inputs);
        case 0x2004:
            return port_player(m.inputs >> 8);
        case 0x2006:
            return m.dsw1;
        }
        return 0;
    }
    return 0; /* unmapped */
}

void SAL_HOT(sal_m68k_write16_slow)(unsigned a, unsigned v)
{
    switch (a >> 16)
    {
    case 0x09:
        m.palette[(a & 0x1fff) >> 1] = (uint8_t)v; /* the low byte only */
        m.palette_dirty = 1;
        break;
    case 0x0a:
        if ((a & 0xffff) == 0)
        {
            outlatch_w(v >> 8);
            intlatch_w(v & 0xff);
        }
        break;
    case 0x0c:
        if ((a & 0xffff) == 0)
            soundlatch_w(v & 0xff);
        /* 0c0004: the watchdog */
        break;
    }
}

void SAL_HOT(sal_m68k_write8_slow)(unsigned a, unsigned v)
{
    switch (a >> 16)
    {
    case 0x09:
        if (a & 1)
        {
            m.palette[(a & 0x1fff) >> 1] = (uint8_t)v;
            m.palette_dirty = 1;
        }
        break;
    case 0x0a:
        if ((a & 0xffff) == 0)
            outlatch_w(v);
        else if ((a & 0xffff) == 1)
            intlatch_w(v);
        break;
    case 0x0c:
        if ((a & 0xffff) == 1)
            soundlatch_w(v);
        break;
    }
}

/* The interrupt line stays asserted (MAME's ASSERT_LINE) until the game
 * clears the enable bit of the interrupt latch: an acknowledge only gives the
 * autovector. */
int SAL_HOT(sal_m68k_int_ack)(int level)
{
    (void)level;
    return M68K_INT_ACK_AUTOVECTOR;
}

/* ---------------------------------------------------------------------------
 * Loading: the ROMs into the parts
 * ------------------------------------------------------------------------ */

typedef struct
{
    const sal_set_info_t *s;
    uint8_t *file[64]; /* each file, as it is */
    void *(*alloc)(uint32_t);
} load_t;

static uint8_t *load_dest(void *arg, int i)
{
    load_t *l = (load_t *)arg;
    if (!l->file[i])
        l->file[i] = (uint8_t *)l->alloc(l->s->files[i].size);
    return l->file[i];
}

sal_files_t sal_load(sal_set_id_t set, int v, const sal_io_t *io, const char *dir, void *(*alloc)(uint32_t),
                     void (*free_fn)(void *), const uint8_t **parts)
{
    const sal_set_info_t *s = &sal_sets[set];
    load_t l;
    memset(&l, 0, sizeof(l));
    l.s = s;
    l.alloc = alloc;
    for (int p = 0; p < SAL_P_COUNT; p++)
        parts[p] = NULL;

    const sal_files_t want = sal_variant_files(set, v);
    const sal_files_t got = sal_load_files(set, want, io, dir, load_dest, &l);
    if (got == want)
    {
        uint8_t *built[SAL_P_COUNT];
        for (int p = 0; p < SAL_P_COUNT; p++)
        {
            built[p] = (uint8_t *)alloc(sal_part_size[p]);
            memset(built[p], 0, sal_part_size[p]);
            parts[p] = built[p];
        }
        for (int i = 0; i < s->nfiles; i++)
        {
            if (!(want & ((sal_files_t)1 << i)))
                continue;
            const sal_romfile_t *f = &s->files[i];
            const uint8_t *src = l.file[i];
            if (f->region == SAL_R_MAIN)
            {
                /* the 68000's words in the host's byte order: byte A of the
                 * CPU's ROM is byte A ^ 1 of the part */
                uint8_t *img = built[SAL_P_MAIN];
                for (uint32_t k = 0; k < f->size; k++)
                    img[(f->offset + k * f->skip) ^ 1] = src[k];
            }
            else
            {
                const int p = f->region == SAL_R_SOUND ? SAL_P_SOUND : f->region == SAL_R_VLM ? SAL_P_VLM : SAL_P_PCM;
                memcpy(built[p] + f->offset, src, f->size);
            }
        }
    }
    for (int i = 0; i < s->nfiles; i++)
        if (l.file[i] && free_fn)
            free_fn(l.file[i]);
    return got;
}

/* ---------------------------------------------------------------------------
 * Life cycle
 * ------------------------------------------------------------------------ */

void sal_init(sal_fmt_t fmt, int samplerate)
{
    m.fmt = fmt;
    m.samplerate = samplerate;
    m.set = -1;
    for (int p = 0; p < 256; p++)
        sal_fetch_page[p] = zero_page;
}

int sal_start(sal_set_id_t set, int variant, const uint8_t *const *parts, sal_alloc_fn fast)
{
    if (set < 0 || set >= SAL_SET_COUNT || variant < 0 || variant >= sal_sets[set].nvariants)
        return 0;
    m.fast_bytes = 0;
    m.rom = (const uint16_t *)parts[SAL_P_MAIN];
    m.rom_sound = parts[SAL_P_SOUND];
    m.rom_vlm = parts[SAL_P_VLM];
    m.rom_pcm = parts[SAL_P_PCM];

    /* the pages: the ROM, its hottest pages copied into SRAM, the work RAM
     * (should code run from it), nothing else */
    for (int p = 0; p < 256; p++)
        sal_fetch_page[p] = zero_page;
    for (int p = 0; p < 0x80; p++)
        sal_fetch_page[p] = m.rom + p * 2048;
    for (int p = 0x80; p < 0x88; p++)
        sal_fetch_page[p] = m.ram + (p - 0x80) * 2048;
    for (int i = 0; i < NHOT; i++)
    {
        uint16_t *copy = fast ? (uint16_t *)fast(4096) : NULL;
        if (!copy)
            break;
        memcpy(copy, m.rom + hot_pages[i] * 2048, 4096);
        sal_fetch_page[hot_pages[i]] = copy;
        m.fast_bytes += 4096;
    }
#if SAL_SOUND_ROM_IN_SRAM
    /* the sound CPU's ROM too, when the SRAM can be spared */
    if (fast)
    {
        uint8_t *copy = (uint8_t *)fast(sal_part_size[SAL_P_SOUND]);
        if (copy)
        {
            memcpy(copy, m.rom_sound, sal_part_size[SAL_P_SOUND]);
            m.rom_sound = copy;
            m.fast_bytes += sal_part_size[SAL_P_SOUND];
        }
    }
#endif

    uint8_t dips[SAL_DIP_PORTS];
    sal_dip_defaults(set, variant, dips);
    sal_set_dips(dips);
    m.set = set;
    m.variant = variant;

    static int cpu_done;
    if (!cpu_done)
    {
        m68k_init();
        m68k_set_cpu_type(M68K_CPU_TYPE_68000);
        cpu_done = 1;
    }
    sal_video_init();
    sal_sound_start();
    sal_reset();
    return 1;
}

int sal_running(void)
{
    return m.set;
}

uint32_t sal_fast_bytes(void)
{
    return m.fast_bytes;
}

const sal_stats_t *sal_stats(void)
{
    m.stats.main_pc = m68k_get_reg(NULL, M68K_REG_PC);
    return &m.stats;
}

void sal_set_dips(const uint8_t *ports)
{
    m.dsw0 = ports[SAL_DIP_DSW0];
    m.dsw1 = ports[SAL_DIP_DSW1];
    m.in0_dips = ports[SAL_DIP_IN0];
}

void sal_reset(void)
{
    if (m.set < 0)
        return;
    memset(m.ram, 0, sizeof(m.ram));
    memset(m.vram, 0, sizeof(m.vram));
    memset(m.charram, 0, sizeof(m.charram));
    memset(m.spriteram, 0, sizeof(m.spriteram));
    memset(m.scroll, 0, sizeof(m.scroll));
    memset(m.palette, 0, sizeof(m.palette));
    m.palette_dirty = 1;
    m.nevents = 0;
    m.frame_cycle = 0;
    memset(&m.stats, 0, sizeof(m.stats));
    /* MAME's machine_reset: both latches cleared */
    m.outlatch = 0;
    intlatch_w(0);
    sal_video_reset();
    m68k_pulse_reset();
}

/* ---------------------------------------------------------------------------
 * Running
 * ------------------------------------------------------------------------ */

/* Runs the main CPU for `cycles`, carrying the overshoot of the instruction
 * that crossed the end into the next slice. */
static int32_t debt;

static void SAL_HOT(run_main)(int32_t base, int32_t cycles)
{
    m.frame_cycle = base;
    const int32_t target = cycles - debt;
    if (target <= 0)
    {
        debt -= cycles;
        return;
    }
    const int used = m68k_execute(target);
    debt = used - target;
}

void SAL_HOT(sal_run_frame)(uint32_t inputs)
{
    if (m.set < 0)
        return;
    m.inputs = inputs;
    m.nevents = 0;
    SAL_PROF_START(t0);
    /* the start of vblank: the interrupt (MAME's vblank_irq1) */
    if (m.irq1_on)
        m68k_set_irq(1);
    run_main(0, (SAL_LINES - SAL_VBLANK_LINE) * SAL_MAIN_PER_LINE);
    /* the picture, down to the next vblank */
    run_main((SAL_LINES - SAL_VBLANK_LINE) * SAL_MAIN_PER_LINE, SAL_VBLANK_LINE * SAL_MAIN_PER_LINE);
    SAL_PROF_ADD(0, t0);
}

int sal_take_events(const sal_event_t **events)
{
    *events = m.events;
    return m.nevents;
}
