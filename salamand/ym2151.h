/*
 * Yamaha YM2151 (OPM), after MAME's sound/ym2151.cpp as of MAME 0.229
 * (license GPL-2.0+, copyright-holders Jarek Burczynski, Ernesto Corvi;
 * version 2.150 final beta, 2002), in plain C for one chip.
 *
 * MAME's version makes a sample per 64 clocks of the chip (55,930 a second
 * for Salamander's 3.579545 MHz). This one makes them at the output rate,
 * as the older MAME core did, with the phase increments, the envelope and
 * LFO timers and the noise clocked by the ratio of the two rates: the same
 * chip, read at fewer points in time. The timers run on, for the status
 * register and CSM, but there is no interrupt line (Salamander does not
 * connect it).
 */
#ifndef YM2151_H
#define YM2151_H

#include <stdint.h>

#define YM_TL_RES_LEN 256
#define YM_TL_TAB_LEN (13 * 2 * YM_TL_RES_LEN)
#define YM_SIN_BITS 10
#define YM_SIN_LEN (1 << YM_SIN_BITS)
#define YM_SIN_MASK (YM_SIN_LEN - 1)

typedef struct
{
    uint32_t phase, freq;
    int32_t dt1;
    uint32_t mul, dt1_i, dt2;
    int32_t *connect;     /* operator output 'direction' */
    int32_t *mem_connect; /* M1 only: where the delayed sample (MEM) goes */
    int32_t mem_value;
    /* channel data, in each channel's operator 0 */
    uint32_t fb_shift;
    int32_t fb_out_curr, fb_out_prev;
    uint32_t kc, kc_i, pms, ams;
    /* */
    uint32_t AMmask;
    uint32_t state; /* 4 attack, 3 decay, 2 sustain, 1 release, 0 off */
    uint8_t eg_sh_ar, eg_sel_ar;
    uint32_t tl;
    int32_t volume;
    uint8_t eg_sh_d1r, eg_sel_d1r;
    uint32_t d1l;
    uint8_t eg_sh_d2r, eg_sel_d2r;
    uint8_t eg_sh_rr, eg_sel_rr;
    uint32_t key, ks, ar, d1r, d2r, rr;
} ym2151_op_t;

typedef struct
{
    int32_t chanout[8];
    int32_t m2, c1, c2, mem;
    ym2151_op_t oper[32];
    uint32_t pan[16];
    uint32_t eg_cnt, eg_timer, eg_timer_add, eg_timer_overflow;
    uint32_t lfo_phase, lfo_timer, lfo_timer_add, lfo_overflow, lfo_counter, lfo_counter_add;
    uint8_t lfo_wsel, amd;
    int8_t pmd;
    uint32_t lfa;
    int32_t lfp;
    uint8_t test, ct;
    uint32_t noise, noise_rng, noise_p, noise_f, noise_add;
    uint32_t csm_req, irq_enable, status;
    uint8_t connect[8];
    uint8_t lastreg;
    /* the timers, counting chip samples in 16.16 */
    uint32_t timer_a_index, timer_b_index;
    uint8_t timer_a_on, timer_b_on;
    int32_t timer_a_left, timer_b_left;
    uint32_t chip_per_out; /* chip samples per output sample, 16.16 */
    /* the tables that depend on the rate */
    int32_t dt1_freq[8 * 32];
    uint32_t freq_scale; /* the chip's rate over the output rate, 16.16 */
} ym2151_t;

void ym2151_init(ym2151_t *ym, uint32_t clock, uint32_t samplerate);
void ym2151_reset(ym2151_t *ym);

/* The two ports: register select (a0 = 0) and data (a0 = 1). */
void ym2151_write(ym2151_t *ym, int a0, uint8_t data);
uint8_t ym2151_read(ym2151_t *ym, int a0);

/* n output samples, the chip's two outputs (not clamped: about 16 bits)
 * summed into l and r. */
void ym2151_render(ym2151_t *ym, int32_t *l, int32_t *r, int n);

#endif
