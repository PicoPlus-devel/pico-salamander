/*
 * Renders a log of YM2151 writes (sal_host --ymlog) through ymfm, Aaron
 * Giles' emulation of the Yamaha chips that MAME uses, into a mono WAV (the
 * chip's two outputs averaged) at the level sal_host gives the chip (1.2, as
 * MAME's Salamander mix). Compare it with sal_host's own WAV of the same run
 * (--only ym, made mono) with hosttest/wavcompare.py to check ym2151.c.
 *
 *   ./hosttest/ym_compare <ymlog> <out.wav>
 *
 * ymfm runs at the chip's own rate (clock / 64 = 55,930 Hz) and is brought
 * to 44.1 kHz by averaging. Built by hosttest/build.sh when ymfm's sources
 * are found (YMFM_DIR).
 */
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "ymfm_opm.h"

#define SAMPLERATE 44100
#define CLOCK 3579545

class intf : public ymfm::ymfm_interface
{
};

struct ev
{
    uint32_t sample;
    int a0, value;
};

static void put16(FILE *f, uint16_t v)
{
    fputc(v & 0xff, f);
    fputc(v >> 8, f);
}

static void put32(FILE *f, uint32_t v)
{
    put16(f, v & 0xffff);
    put16(f, v >> 16);
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <ymlog> <out.wav>\n", argv[0]);
        return 1;
    }
    FILE *in = fopen(argv[1], "r");
    if (!in)
    {
        perror(argv[1]);
        return 1;
    }
    std::vector<ev> evs;
    ev e;
    while (fscanf(in, "%u %d %d", &e.sample, &e.a0, &e.value) == 3)
        evs.push_back(e);
    fclose(in);
    if (evs.empty())
        return 1;
    const uint32_t total = evs.back().sample + SAMPLERATE; /* a second after the last write */

    intf i0;
    ymfm::ym2151 chip(i0);
    chip.reset();
    const double rate = chip.sample_rate(CLOCK);
    printf("ymfm at %.0f Hz, %zu writes, %u samples\n", rate, evs.size(), total);

    std::vector<int16_t> out(total);
    size_t next = 0;
    double pos = 0; /* ymfm samples made */
    for (uint32_t n = 0; n < total; n++)
    {
        while (next < evs.size() && evs[next].sample <= n)
        {
            chip.write(evs[next].a0, (uint8_t)evs[next].value);
            next++;
        }
        /* the ymfm samples of this output sample, averaged */
        const double upto = (n + 1) * rate / SAMPLERATE;
        double v = 0;
        int cnt = 0;
        while (pos < upto)
        {
            ymfm::ym2151::output_data o;
            chip.generate(&o, 1);
            v += (o.data[0] + o.data[1]) / 2.0;
            cnt++;
            pos += 1;
        }
        if (cnt)
            v /= cnt;
        v *= 1.2;
        if (v > 32767)
            v = 32767;
        else if (v < -32768)
            v = -32768;
        out[n] = (int16_t)lrint(v);
    }

    FILE *f = fopen(argv[2], "wb");
    if (!f)
    {
        perror(argv[2]);
        return 1;
    }
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + total * 2);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 1);
    put32(f, SAMPLERATE);
    put32(f, SAMPLERATE * 2);
    put16(f, 2);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, total * 2);
    fwrite(out.data(), 2, total, f);
    fclose(f);
    return 0;
}
