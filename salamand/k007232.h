/*
 * Konami 007232 PCM controller, after MAME's sound/k007232.cpp (license
 * BSD-3-Clause, copyright-holders Nicola Salmoria, Hiromitsu Shioya). Two
 * channels of 7-bit samples from a ROM (the eighth bit ends a sample), each
 * with its own volume on each of the two outputs; here as Salamander wires
 * it: no bank switching, 128 KB of samples.
 */
#ifndef K007232_H
#define K007232_H

#include <stdint.h>

typedef struct
{
    uint32_t addr, start;
    uint32_t counter;
    uint32_t step;
    uint8_t play;
    uint8_t vol[2]; /* on outputs A and B */
} k007232_channel_t;

typedef struct
{
    const uint8_t *rom; /* 128 KB */
    uint32_t rom_mask;
    k007232_channel_t ch[2];
    uint8_t wreg[14];
    /* the chip's own rate (clock / 128) against the output rate: 16.16 */
    uint32_t rate_step, rate_acc;
    int32_t hold_a, hold_b; /* the last chip samples, held between them */
} k007232_t;

void k007232_init(k007232_t *k, const uint8_t *rom, uint32_t rom_size, uint32_t clock, uint32_t samplerate);
void k007232_reset(k007232_t *k);

/* Registers 0..13. Register 12 is the external port: the caller sets the
 * volumes from it (k007232_set_volume). */
void k007232_write(k007232_t *k, int reg, uint8_t data);
uint8_t k007232_read(k007232_t *k, int reg);
void k007232_set_volume(k007232_t *k, int channel, int vol_a, int vol_b);

/* n output samples of the two outputs, summed into a and b (the raw sums of
 * sample x volume x 2, as MAME's, about -32640..32640 at most). */
void k007232_render(k007232_t *k, int32_t *a, int32_t *b, int n);

#endif
