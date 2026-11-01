/*
 * A smoke test of the YM2151 port: one channel with a plain patch, keyed on
 * for a while, then off. Prints the output's level over time; no sound means
 * the chip is broken.
 *
 *   ./hosttest/ym_test [out.wav]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ym2151.h"

#define RATE 44100

static void reg(ym2151_t *ym, int r, int v)
{
    ym2151_write(ym, 0, (uint8_t)r);
    ym2151_write(ym, 1, (uint8_t)v);
}

int main(int argc, char **argv)
{
    static ym2151_t ym;
    ym2151_init(&ym, 3579545, RATE);
    ym2151_reset(&ym);
    /* channel 0: algorithm 7 (all four operators to the output), both
     * speakers, no feedback; operators: MUL 1, TL 0 (loud) for the carriers,
     * fast attack, no decay, sustain, slow release; A440 = KC 0x4a */
    reg(&ym, 0x20, 0xc7);
    for (int op = 0; op < 4; op++)
    {
        reg(&ym, 0x40 + op * 8, 0x01); /* DT1 0, MUL 1 */
        reg(&ym, 0x60 + op * 8, op == 0 ? 0x08 : 0x7f); /* TL: only M1 heard */
        reg(&ym, 0x80 + op * 8, 0x1f); /* KS 0, AR 31 */
        reg(&ym, 0xa0 + op * 8, 0x00); /* D1R 0 */
        reg(&ym, 0xc0 + op * 8, 0x00); /* D2R 0 */
        reg(&ym, 0xe0 + op * 8, 0x0f); /* D1L 0, RR 15 */
    }
    reg(&ym, 0x28, 0x4a); /* KC */
    reg(&ym, 0x30, 0x00); /* KF */
    reg(&ym, 0x08, 0x78); /* key on, all operators, channel 0 */

    FILE *wav = argc > 1 ? fopen(argv[1], "wb") : NULL;
    if (wav)
        fwrite("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\x44\xac\0\0\x88\x58\x01\0\x02\0\x10\0data\0\0\0\0", 1, 44, wav);
    int zero_crossings = 0, prev = 0;
    for (int t = 0; t < 20; t++) /* 20 x 50 ms */
    {
        if (t == 12)
            reg(&ym, 0x08, 0x00); /* key off */
        int32_t l[2205], r[2205];
        memset(l, 0, sizeof(l));
        memset(r, 0, sizeof(r));
        ym2151_render(&ym, l, r, 2205);
        int32_t peak = 0;
        double rms = 0;
        for (int i = 0; i < 2205; i++)
        {
            const int32_t v = l[i] < 0 ? -l[i] : l[i];
            if (v > peak)
                peak = v;
            rms += (double)l[i] * l[i];
            if ((l[i] < 0) != (prev < 0))
                zero_crossings++;
            prev = l[i];
            if (wav)
            {
                int16_t s = (int16_t)(l[i] > 32767 ? 32767 : l[i] < -32768 ? -32768 : l[i]);
                fwrite(&s, 2, 1, wav);
            }
        }
        printf("%4d ms: peak %6d rms %7.0f status %02x\n", t * 50, peak, rms > 0 ? __builtin_sqrt(rms / 2205) : 0.0,
               ym2151_read(&ym, 1));
    }
    printf("zero crossings in 1 s: %d (A440 would give about 880 while on)\n", zero_crossings);
    if (wav)
        fclose(wav);
    return 0;
}
