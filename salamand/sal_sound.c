/*
 * Salamander: the sound board, after MAME's konami/nemesis.cpp (license
 * BSD-3-Clause, copyright-holders Bryan McPhail).
 *
 * Sound CPU (Z80, 3.579545 MHz):
 *   0000-7fff  ROM
 *   8000-87ff  RAM
 *   a000  (r)  the latch the main CPU writes
 *   b000-b00d  K007232 (register 12 sets the volumes: the high nibble of
 *              channel A on output A, the low nibble of channel B on output B)
 *   c000-c001  YM2151 (address, data)
 *   d000  (w)  VLM5030 data
 *   e000  (r)  VLM5030 busy
 *   f000  (w)  VLM5030 b0 reset, b1 start
 * Interrupts (IRQ, held until taken) come from the main CPU's out latch.
 *
 * The main CPU only ever reaches this board through the latch and the
 * interrupt line, so the board runs a frame at a time after the main CPU,
 * from that frame's writes and interrupts, timestamped in main CPU cycles.
 * That is exact: whatever the sound CPU reads from the latch at a given cycle
 * is what the main CPU had last written by then. It lets the board run on
 * the same core after each frame, or on core1 a frame behind.
 *
 * The chips are rendered up to the current cycle before each register write,
 * so every write lands at its own sample. MAME's mix: the YM2151 at 1.2 with
 * its outputs swapped, the K007232 at 0.08, the VLM5030 at 2.5, all into
 * both speakers except the YM2151's.
 */
#include "sal_internal.h"

#include "k007232.h"
#include "vlm5030.h"
#include "ym2151.h"

#define YM_CLOCK 3579545
#define PCM_CLOCK 3579545
#define VLM_CLOCK 3579545

/* MAME's levels in Q8 of the chips' own full scale: the YM2151's 16 bits,
 * the K007232's 32640 (sample x volume x 2), the VLM5030's 10 bits */
#define YM_GAIN_Q8 307  /* 1.2 */
#define PCM_GAIN_Q8 20  /* 0.08 */
#define VLM_GAIN 160    /* 2.5 x 32768 / 512 */

#define MAX_CHUNK 64

typedef struct
{
    z80_t cpu;
    uint8_t ram[0x800];
    uint8_t latch;
    ym2151_t ym;
    k007232_t pcm;
    vlm5030_t vlm;

    /* the frame being made */
    int16_t *out;        /* stereo, interleaved */
    int nout, pos;       /* samples in the frame, and made so far */
    int32_t frame_cycle; /* sound CPU cycles of the frame done by the current slice's start */
    int32_t slice_start; /* the CPU's icount at the start of the slice */
    uint32_t idle_cycles;
} sound_t;

static sound_t snd;

/* The host harness's choice of what is heard (--only): bit 0 the YM2151,
 * bit 1 the K007232, bit 2 the VLM5030. */
int sal_snd_mask = 7;

/* The host harness's log of the YM2151's writes (--ymlog); NULL on the board. */
void (*sal_ym_log)(int a0, uint8_t value, int sample);

/* ---------------------------------------------------------------------------
 * Audio
 * ------------------------------------------------------------------------ */

static inline int16_t SAL_HOT(clamp16)(int32_t v)
{
    if (v > 32767)
        return 32767;
    if (v < -32768)
        return -32768;
    return (int16_t)v;
}

/* Renders the chips up to sample `upto` of the frame. */
static void SAL_HOT(render_upto)(int upto)
{
    if (upto > snd.nout)
        upto = snd.nout;
    while (snd.pos < upto)
    {
        int n = upto - snd.pos;
        if (n > MAX_CHUNK)
            n = MAX_CHUNK;
        int32_t l[MAX_CHUNK], r[MAX_CHUNK], a[MAX_CHUNK], b[MAX_CHUNK], v[MAX_CHUNK];
        for (int i = 0; i < n; i++)
            l[i] = r[i] = a[i] = b[i] = v[i] = 0;
        SAL_PROF_START(t0);
        ym2151_render(&snd.ym, l, r, n);
        SAL_PROF_ADD(2, t0);
        SAL_PROF_START(t1);
        k007232_render(&snd.pcm, a, b, n);
        vlm5030_render(&snd.vlm, v, n);
        SAL_PROF_ADD(3, t1);
        const int gy = (sal_snd_mask & 1) ? YM_GAIN_Q8 : 0;
        const int gp = (sal_snd_mask & 2) ? PCM_GAIN_Q8 : 0;
        const int gv = (sal_snd_mask & 4) ? VLM_GAIN : 0;
        int16_t *out = snd.out + 2 * snd.pos;
        for (int i = 0; i < n; i++)
        {
            const int32_t common = ((a[i] + b[i]) * gp >> 8) + v[i] * gv;
            /* the YM2151's outputs are swapped on the board (MAME) */
            out[2 * i] = clamp16((r[i] * gy >> 8) + common);
            out[2 * i + 1] = clamp16((l[i] * gy >> 8) + common);
        }
        snd.pos += n;
    }
}

/* The sample of the frame the sound CPU is at now. */
static inline int SAL_HOT(sample_now)(void)
{
    int32_t cycle = snd.frame_cycle + (snd.slice_start - snd.cpu.icount);
    if (cycle < 0)
        cycle = 0;
    /* 59062 * 735 fits in 32 bits: no 64-bit division (in flash) */
    return (int)(((uint32_t)cycle * (uint32_t)snd.nout) / SAL_SOUND_PER_FRAME);
}

/* ---------------------------------------------------------------------------
 * Sound CPU memory
 * ------------------------------------------------------------------------ */

/* The chips are brought up to now before they are read, as MAME's stream
 * update does: the YM2151's timer flags, which the sound program polls for
 * its tempo, the VLM5030's busy flag and the K007232's start-on-read all
 * depend on the time. */
static uint8_t SAL_HOT(s_read)(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    switch (addr & 0xf000)
    {
    case 0xa000:
        return snd.latch;
    case 0xb000:
        if ((addr & 0xfff) <= 13)
        {
            render_upto(sample_now());
            return k007232_read(&snd.pcm, addr & 0xf);
        }
        return 0;
    case 0xc000:
        render_upto(sample_now());
        return ym2151_read(&snd.ym, addr & 1);
    case 0xe000:
        render_upto(sample_now());
        return (uint8_t)vlm5030_bsy_r(&snd.vlm);
    }
    return 0;
}

static void SAL_HOT(s_write)(z80_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    switch (addr & 0xf000)
    {
    case 0xb000:
        if ((addr & 0xfff) <= 13)
        {
            render_upto(sample_now());
            k007232_write(&snd.pcm, addr & 0xf, value);
            if ((addr & 0xf) == 12)
            {
                /* MAME's volume_callback */
                k007232_set_volume(&snd.pcm, 0, (value >> 4) * 0x11, 0);
                k007232_set_volume(&snd.pcm, 1, 0, (value & 0x0f) * 0x11);
            }
        }
        break;
    case 0xc000:
    {
        const int now = sample_now();
        if (addr & 1)
            render_upto(now); /* a register: up to now with the old value */
        if (sal_ym_log)
            sal_ym_log(addr & 1, value, now);
        ym2151_write(&snd.ym, addr & 1, value);
        break;
    }
    case 0xd000:
        vlm5030_data_w(&snd.vlm, value);
        break;
    case 0xf000:
        render_upto(sample_now());
        vlm5030_rst_w(&snd.vlm, value & 1);
        vlm5030_st_w(&snd.vlm, (value >> 1) & 1);
        break;
    }
}

static uint8_t s_in(z80_t *cpu, uint16_t port)
{
    (void)cpu;
    (void)port;
    return 0xff;
}

static void s_out(z80_t *cpu, uint16_t port, uint8_t value)
{
    (void)cpu;
    (void)port;
    (void)value;
}

/* ---------------------------------------------------------------------------
 * Life cycle
 * ------------------------------------------------------------------------ */

void sal_sound_start(void)
{
    ym2151_init(&snd.ym, YM_CLOCK, (uint32_t)m.samplerate);
    k007232_init(&snd.pcm, m.rom_pcm, sal_part_size[SAL_P_PCM], PCM_CLOCK, (uint32_t)m.samplerate);
    vlm5030_init(&snd.vlm, m.rom_vlm, sal_part_size[SAL_P_VLM], VLM_CLOCK, (uint32_t)m.samplerate);
    sal_sound_reset();
}

void sal_sound_reset(void)
{
    z80_t *cpu = &snd.cpu;
    memset(snd.ram, 0, sizeof(snd.ram));
    snd.latch = 0;
    z80_init(cpu);
    z80_reset(cpu);
    for (int p = 0; p < 256; p++)
    {
        cpu->rd_page[p] = NULL;
        cpu->wr_page[p] = NULL;
    }
    for (int p = 0; p < 0x80; p++)
        cpu->rd_page[p] = m.rom_sound + p * 256;
    for (int p = 0; p < 8; p++)
        cpu->rd_page[0x80 + p] = cpu->wr_page[0x80 + p] = snd.ram + p * 256;
    cpu->read = s_read;
    cpu->write = s_write;
    cpu->in = s_in;
    cpu->out = s_out;
    ym2151_reset(&snd.ym);
    k007232_reset(&snd.pcm);
    vlm5030_reset(&snd.vlm);
}

/* ---------------------------------------------------------------------------
 * A frame
 * ------------------------------------------------------------------------ */

/* Runs the sound CPU for `cycles`, about a line at a time. A CPU halted with
 * no interrupt to take only waits: its cycles are counted instead of run. */
static void SAL_HOT(run_sound)(int cycles)
{
    z80_t *cpu = &snd.cpu;
    while (cycles > 0)
    {
        const int n = cycles < SAL_SOUND_PER_LINE ? cycles : SAL_SOUND_PER_LINE;
        if (cpu->halt && !cpu->irq_state)
        {
            cpu->icount += n;
            if (cpu->icount > 0)
                cpu->icount = 0;
            snd.idle_cycles += (uint32_t)n;
        }
        else
        {
            snd.slice_start = cpu->icount + n;
            z80_run(cpu, n);
        }
        snd.frame_cycle += n;
        cycles -= n;
    }
}

/* A main CPU cycle of the frame as a sound CPU cycle: 59062 / 152064. */
static inline int32_t SAL_HOT(sound_cycle)(uint32_t main_cycle)
{
    return (int32_t)(((main_cycle >> 2) * (uint32_t)SAL_SOUND_PER_FRAME) / (SAL_MAIN_PER_FRAME / 4));
}

void SAL_HOT(sal_sound_frame)(const sal_event_t *events, int nevents, int16_t *audio, int n)
{
    for (int i = 0; i < 2 * n; i++)
        audio[i] = 0;
    if (m.set < 0)
        return;
    snd.out = audio;
    snd.nout = n;
    snd.pos = 0;
    snd.frame_cycle = 0;
    snd.slice_start = snd.cpu.icount;
    snd.idle_cycles = 0;

    SAL_PROF_START(t0);
    int ev = 0;
    int32_t t = 0;
    while (t < SAL_SOUND_PER_FRAME)
    {
        int32_t until = SAL_SOUND_PER_FRAME;
        if (ev < nevents && sound_cycle(events[ev].cycle) < until)
            until = sound_cycle(events[ev].cycle);
        run_sound(until - t);
        t = until;
        while (ev < nevents && sound_cycle(events[ev].cycle) <= t)
        {
            const sal_event_t *e = &events[ev++];
            if (e->kind == SAL_EV_LATCH)
                snd.latch = e->value;
            else if (e->kind == SAL_EV_IRQ)
                z80_hold_irq(&snd.cpu, 0xff);
        }
    }
    render_upto(n);
    SAL_PROF_ADD(1, t0); /* the whole board, the chips (2, 3) included */
    m.stats.sound_pc = snd.cpu.pc.w;
    m.stats.sound_idle_cycles = snd.idle_cycles;
}
