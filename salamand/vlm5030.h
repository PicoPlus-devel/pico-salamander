/*
 * Sanyo VLM5030 speech synthesizer, after MAME's sound/vlm5030.cpp (license
 * BSD-3-Clause, copyright-holder Tatsuyuki Satoh) with its coefficients from
 * tms5110r.hxx. The chip reads LPC frames from its own ROM (16 KB here) and
 * makes 8,135 samples a second (clock / 440).
 */
#ifndef VLM5030_H
#define VLM5030_H

#include <stdint.h>

typedef struct
{
    const uint8_t *rom;
    uint32_t rom_mask;

    uint16_t address;
    uint8_t pin_bsy, pin_st, pin_vcu, pin_rst;
    uint8_t latch_data;
    uint16_t vcu_addr_h;
    uint8_t parameter;
    uint8_t phase;
    int frame_size;
    int pitch_offset;
    uint8_t interp_step;
    uint8_t interp_count;
    uint8_t sample_count;
    uint8_t pitch_count;
    uint16_t old_energy, target_energy, new_energy;
    uint8_t old_pitch, target_pitch, new_pitch;
    int16_t old_k[10], target_k[10], new_k[10];
    unsigned current_energy, current_pitch;
    int current_k[10];
    int32_t x[10];
    uint32_t noise; /* in place of MAME's machine().rand() */

    /* the chip's rate against the output rate: 16.16 */
    uint32_t rate_step, rate_acc;
    int32_t hold;
} vlm5030_t;

void vlm5030_init(vlm5030_t *v, const uint8_t *rom, uint32_t rom_size, uint32_t clock, uint32_t samplerate);
void vlm5030_reset(vlm5030_t *v);

int vlm5030_bsy_r(vlm5030_t *v);
void vlm5030_data_w(vlm5030_t *v, uint8_t data);
void vlm5030_rst_w(vlm5030_t *v, int state);
void vlm5030_st_w(vlm5030_t *v, int state);

/* n output samples summed into out (the chip's 10-bit output, -512..511). */
void vlm5030_render(vlm5030_t *v, int32_t *out, int n);

#endif
