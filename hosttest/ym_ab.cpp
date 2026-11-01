/*
 * A/B tests of ym2151.c against ymfm: the same register sequences into both,
 * one feature at a time, a mono WAV of each (mine_N.wav, ymfm_N.wav in the
 * given directory) for hosttest/wavcompare.py, and a summary line per test
 * (the level curves' correlation and the spectra's similarity, as
 * wavcompare.py computes them, over 50 ms windows).
 *
 *   ./hosttest/ym_ab <outdir>
 */
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "ym2151.h"
}
#include "ymfm_opm.h"

#define RATE 44100
#define CLOCK 3579545
#define SECONDS 1.5

class intf : public ymfm::ymfm_interface
{
};

struct step
{
    double t; /* seconds */
    int reg, val;
};

typedef std::vector<step> script;

static void render_mine(const script &sc, std::vector<double> &out)
{
    static ym2151_t ym;
    ym2151_init(&ym, CLOCK, RATE);
    ym2151_reset(&ym);
    const int total = (int)(SECONDS * RATE);
    out.assign(total, 0);
    size_t next = 0;
    for (int n = 0; n < total; n++)
    {
        while (next < sc.size() && sc[next].t * RATE <= n)
        {
            ym2151_write(&ym, 0, (uint8_t)sc[next].reg);
            ym2151_write(&ym, 1, (uint8_t)sc[next].val);
            next++;
        }
        int32_t l = 0, r = 0;
        ym2151_render(&ym, &l, &r, 1);
        out[n] = (l + r) / 2.0;
    }
}

static void render_ymfm(const script &sc, std::vector<double> &out)
{
    intf i0;
    ymfm::ym2151 chip(i0);
    chip.reset();
    const double rate = chip.sample_rate(CLOCK);
    const int total = (int)(SECONDS * RATE);
    out.assign(total, 0);
    size_t next = 0;
    double pos = 0;
    for (int n = 0; n < total; n++)
    {
        while (next < sc.size() && sc[next].t * RATE <= n)
        {
            chip.write(0, (uint8_t)sc[next].reg);
            chip.write(1, (uint8_t)sc[next].val);
            next++;
        }
        const double upto = (n + 1) * rate / RATE;
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
        out[n] = cnt ? v / cnt : 0;
    }
}

static void wav(const std::string &path, const std::vector<double> &x)
{
    FILE *f = fopen(path.c_str(), "wb");
    const uint32_t n = (uint32_t)x.size();
    auto p16 = [&](uint16_t v) { fputc(v & 0xff, f); fputc(v >> 8, f); };
    auto p32 = [&](uint32_t v) { p16(v & 0xffff); p16(v >> 16); };
    fwrite("RIFF", 1, 4, f); p32(36 + n * 2); fwrite("WAVEfmt ", 1, 8, f); p32(16); p16(1); p16(1); p32(RATE); p32(RATE * 2); p16(2); p16(16);
    fwrite("data", 1, 4, f); p32(n * 2);
    for (double v : x)
    {
        int16_t s = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

/* the metrics of wavcompare.py: windows of 50 ms */
static void compare(const char *name, const std::vector<double> &a, const std::vector<double> &b)
{
    const int win = RATE / 20;
    const int nw = (int)a.size() / win;
    std::vector<double> dba, dbb;
    double sim_sum = 0, sim_min = 1;
    int loud = 0, peakmatch = 0;
    for (int w = 0; w < nw; w++)
    {
        double ea = 0, eb = 0;
        for (int i = 0; i < win; i++)
        {
            ea += a[w * win + i] * a[w * win + i];
            eb += b[w * win + i] * b[w * win + i];
        }
        ea = sqrt(ea / win) + 1e-9;
        eb = sqrt(eb / win) + 1e-9;
        if (ea < 30 || eb < 30)
            continue;
        loud++;
        dba.push_back(20 * log10(ea));
        dbb.push_back(20 * log10(eb));
        /* magnitude spectra by a plain DFT at 20 Hz steps up to 11 kHz */
        const int bins = 550;
        std::vector<double> fa(bins), fb(bins);
        double na = 0, nb = 0, dot = 0;
        int pa = 0, pb = 0;
        for (int k = 1; k < bins; k++)
        {
            double ra = 0, ia = 0, rb = 0, ib = 0;
            const double wk = 2 * M_PI * k / win;
            for (int i = 0; i < win; i++)
            {
                const double h = 0.5 - 0.5 * cos(2 * M_PI * i / win);
                const double c = cos(wk * i), s = sin(wk * i);
                ra += a[w * win + i] * h * c;
                ia -= a[w * win + i] * h * s;
                rb += b[w * win + i] * h * c;
                ib -= b[w * win + i] * h * s;
            }
            fa[k] = sqrt(ra * ra + ia * ia);
            fb[k] = sqrt(rb * rb + ib * ib);
            na += fa[k] * fa[k];
            nb += fb[k] * fb[k];
            dot += fa[k] * fb[k];
            if (fa[k] > fa[pa]) pa = k;
            if (fb[k] > fb[pb]) pb = k;
        }
        const double sim = dot / (sqrt(na) * sqrt(nb) + 1e-9);
        sim_sum += sim;
        if (sim < sim_min)
            sim_min = sim;
        if (fabs(pa - pb) <= 1)
            peakmatch++;
    }
    double corr = 0;
    if (dba.size() > 2)
    {
        double ma = 0, mb = 0;
        for (size_t i = 0; i < dba.size(); i++) { ma += dba[i]; mb += dbb[i]; }
        ma /= dba.size(); mb /= dbb.size();
        double sab = 0, saa = 0, sbb = 0;
        for (size_t i = 0; i < dba.size(); i++) { sab += (dba[i] - ma) * (dbb[i] - mb); saa += (dba[i] - ma) * (dba[i] - ma); sbb += (dbb[i] - mb) * (dbb[i] - mb); }
        corr = sab / (sqrt(saa * sbb) + 1e-9);
    }
    double la = 0, lb = 0;
    for (size_t i = 0; i < dba.size(); i++) { la += dba[i]; lb += dbb[i]; }
    printf("%-28s windows %3d  level mine %6.1f dB ymfm %6.1f dB  curve corr %6.3f  spectra mean %.3f min %.3f  peak same %3d%%\n", name, loud,
           loud ? la / loud : 0, loud ? lb / loud : 0, corr, loud ? sim_sum / loud : 0, loud ? sim_min : 0, loud ? 100 * peakmatch / loud : 0);
}

/* a channel's four operators: DT1/MUL, TL, KS/AR, AMS-EN/D1R, DT2/D2R, D1L/RR */
static void op(script &s, double t, int ch, int o, int dtmul, int tl, int ksar, int d1r, int dt2d2r, int d1lrr)
{
    const int r = ch + o * 8;
    s.push_back({t, 0x40 + r, dtmul});
    s.push_back({t, 0x60 + r, tl});
    s.push_back({t, 0x80 + r, ksar});
    s.push_back({t, 0xa0 + r, d1r});
    s.push_back({t, 0xc0 + r, dt2d2r});
    s.push_back({t, 0xe0 + r, d1lrr});
}

/* a plain patch on channel ch: algorithm alg, feedback fb, M1 at tl, the
 * others at tl2; KC kc; keyed on at t and off at toff */
static script patch(int ch, int alg, int fb, int tl, int tl2, int kc, double toff, int ar = 0x1f, int d1r = 0, int d2r = 0, int d1lrr = 0x0f)
{
    script s;
    s.push_back({0, 0x20 + ch, 0xc0 | (fb << 3) | alg});
    op(s, 0, ch, 0, 0x01, tl, ar, d1r, d2r, d1lrr);
    op(s, 0, ch, 1, 0x01, tl2, ar, d1r, d2r, d1lrr);
    op(s, 0, ch, 2, 0x01, tl2, ar, d1r, d2r, d1lrr);
    op(s, 0, ch, 3, 0x01, tl2, ar, d1r, d2r, d1lrr);
    s.push_back({0, 0x28 + ch, kc});
    s.push_back({0, 0x30 + ch, 0});
    s.push_back({0, 0x38 + ch, 0});
    s.push_back({0.01, 0x08, 0x78 | ch});
    s.push_back({toff, 0x08, ch});
    return s;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <outdir>\n", argv[0]);
        return 1;
    }
    const std::string dir = argv[1];
    struct test
    {
        const char *name;
        script s;
    };
    std::vector<test> tests;
    tests.push_back({"sine alg7 M1", patch(0, 7, 0, 8, 0x7f, 0x4a, 1.0)});
    tests.push_back({"alg0 4-op chain", patch(1, 0, 0, 20, 30, 0x4a, 1.0)});
    tests.push_back({"alg4 two pairs", patch(2, 4, 0, 16, 24, 0x3a, 1.0)});
    tests.push_back({"feedback 6 alg7", patch(3, 7, 6, 10, 0x7f, 0x4a, 1.0)});
    tests.push_back({"envelope AR15 D1R12 D1L4 D2R6 RR6", patch(4, 7, 0, 8, 0x7f, 0x4a, 0.8, 0x0f, 0x0c, 0x06, 0x46)});
    tests.push_back({"key scaling KS3 high KC", patch(5, 7, 0, 8, 0x7f, 0x7a, 0.8, 0xdf, 0x0a, 0x00, 0x2f)});
    {
        script s = patch(6, 7, 0, 8, 0x7f, 0x4a, 1.0);
        /* vibrato: LFO 0xc8, PMD 127, PMS 7, triangle */
        s.push_back({0, 0x18, 0xc8});
        s.push_back({0, 0x19, 0xff});
        s.push_back({0, 0x1b, 0x02});
        s.push_back({0, 0x38 + 6, 0x70});
        tests.push_back({"LFO vibrato PMS7", s});
    }
    {
        script s = patch(0, 7, 0, 8, 0x7f, 0x4a, 1.0);
        /* tremolo: AMD 127, AMS 3, AM on M1 */
        s.push_back({0, 0x18, 0xa0});
        s.push_back({0, 0x19, 0x7f});
        s.push_back({0, 0x1b, 0x00});
        s.push_back({0, 0x38, 0x03});
        s.push_back({0, 0xa0, 0x80});
        tests.push_back({"LFO tremolo AMS3 saw", s});
    }
    {
        script s = patch(1, 7, 0, 8, 0x7f, 0x4a, 1.0);
        s.push_back({0, 0x40 + 1, 0x31}); /* DT1 3, MUL 1 on M1 */
        s.push_back({0, 0xc0 + 1, 0x40}); /* DT2 1 */
        tests.push_back({"detune DT1 3 DT2 1", s});
    }
    {
        script s = patch(2, 7, 0, 8, 0x7f, 0x4a, 1.0);
        s.push_back({0, 0x30 + 2, 0x80}); /* KF 32 of 64: a quarter tone */
        tests.push_back({"key fraction 32", s});
    }
    {
        script s = patch(7, 7, 0, 0x7f, 0x7f, 0x4a, 1.0);
        s.push_back({0, 0x60 + 7 + 24, 8}); /* C2 of channel 7 loud */
        s.push_back({0, 0x0f, 0x80 | 0x10}); /* noise on, period 16 */
        tests.push_back({"noise channel 7", s});
    }
    {
        script s = patch(3, 2, 2, 24, 40, 0x5e, 0.6, 0x1f, 0x08, 0x04, 0x38);
        s.push_back({0.7, 0x08, 0x78 | 3}); /* a second note, no KC change */
        s.push_back({1.2, 0x08, 3});
        tests.push_back({"alg2 fb2 retrigger", s});
    }
    {
        script s = patch(4, 7, 0, 8, 0x7f, 0x4a, 1.4, 0x08, 0x00, 0x00, 0x0f); /* slow attack 8 */
        tests.push_back({"slow attack AR8", s});
    }
    for (size_t i = 0; i < tests.size(); i++)
    {
        std::vector<double> a, b;
        render_mine(tests[i].s, a);
        render_ymfm(tests[i].s, b);
        wav(dir + "/mine_" + std::to_string(i) + ".wav", a);
        wav(dir + "/ymfm_" + std::to_string(i) + ".wav", b);
        compare(tests[i].name, a, b);
    }
    return 0;
}
