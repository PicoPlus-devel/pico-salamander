/*
 * Host harness for the Salamander core: loads a set through the same loader
 * as the firmware, runs it, dumps frames as PPM and the audio as WAV, or
 * plays it in a window (live mode).
 *
 *   ./hosttest/sal_host <romroot> [salamand|lifefrce] <frames> <dump-every-N> [outdir] [options]
 *
 * <romroot> holds the set folders as on the SD card (SALAMAND, LIFEFRCE);
 * without a set the first complete one is run, Salamander before Lifeforce.
 *
 * Options:
 *   --variant N    the version of the set (Salamander: 0 version D, 1 version
 *                  J, 2 Tecfri; Lifeforce: 0 US, 1 Japan); by default the
 *                  first one found
 *   --wav FILE     write the audio (stereo, 16-bit, 44.1 kHz)
 *   --coin F       insert a coin at frame F (held for 6 frames)
 *   --start F      press 1P start at frame F (held for 6 frames)
 *   --play F       from frame F: shoot, steer around, now and then power up
 *   --keys F:MASK  hold inputs MASK (SAL_IN_*, hex) from frame F for 6 frames
 *   --video FILE   every frame, as raw RGB24 (320x240), for
 *                  ffmpeg -f rawvideo -pix_fmt rgb24 -s 320x240 -r 60 -i FILE
 *   --ymlog FILE   log every write to the YM2151 (for hosttest/ym_compare)
 *   --only ym|pcm|vlm  hear only one of the chips
 *   --trace F      print the CPUs' PCs at the end of frames F..F+59
 *   --layers M     draw only the characters (1) or only the sprites (2)
 *   --dumpram F    write the video RAM, character RAM, sprite RAM, scroll RAM
 *                  and palette at the end of frame F (outdir/ram_F_*.bin)
 *   --hash FILE    write, per frame, a CRC32 of the main CPU's registers and
 *                  of its RAM, for comparing two builds of the core (a run
 *                  of the same set with the same input must match)
 *   --profile FILE write what the main CPU ran: instructions per 4 KB page of
 *                  the ROM (FILE.pages) and per opcode handler of the 68000
 *                  core (FILE.handlers), most first: what to keep in SRAM on
 *                  the board (sal.c's hot_pages, tools/m68k_dispatch.py --hot)
 *
 * Every dumped frame is outdir/frame_NNNNN.ppm (320x240, the canvas the board
 * shows: the 256x224 picture centred). A summary line per second reports the
 * time spent per frame and what the machine did.
 *
 * Live mode (built with SDL2, see build.sh):
 *
 *   ./hosttest/sal_host <romroot> [salamand|lifefrce] [frames] [dump-every-N] [outdir] --live [options]
 *
 * shows the frames in a window, plays the sound and reads the keyboard, in
 * real time. Without a frame count it runs until the window is closed; frames
 * are dumped with F12, and every Nth when N is given. Scripted input is ORed
 * with the keyboard, and --wav and --video record what was played.
 *   --scale N      window size, N times the picture (default 3)
 *   --mute         no sound output; paced on a timer
 * The keys are those of a USB keyboard on the board, mapped as on the board
 * (sal_input.c): arrows joystick, X shoot, Z power-up, C or V missile, A coin
 * (when released), S start; also MAME's 5 coin, 1 start, 2 two player start.
 * A+S resets the game (the board opens its settings menu). Space pauses, N
 * steps a frame while paused, Tab (held) fast-forwards, F12 dumps the frame
 * on screen, Esc quits.
 */
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "sal.h"
#include "sal_input.h"
#include "sal_internal.h" /* --dumpram */
#if !SAL_HOST_REF
#include "sal_m68k_dispatch.h"
#include "sal_m68k_names.h"
#endif
#include "m68k.h"
#include "miniz.h"
#if HOST_SDL
#include "host_sdl.h"
#endif

#define SAMPLERATE 44100
#define SAMPLES_PER_FRAME 735
#define MAX_KEYS 64
#define CANVAS_W 320
#define CANVAS_H 240
#define PIC_X ((CANVAS_W - SAL_W) / 2)
#define PIC_Y ((CANVAS_H - SAL_H) / 2)

#if SAL_FRAME_STATS
/* the core's profiling (sal_internal.h): nanoseconds here */
extern uint32_t sal_prof[8];
uint32_t sal_cycles(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000000000ull + ts.tv_nsec);
}
#endif

/* --ymlog: every write to the YM2151, for hosttest/ym_compare. A line per
 * write: the output sample it lands on, the port, the value. */
extern void (*sal_ym_log)(int a0, uint8_t value, int sample);
extern int sal_snd_mask;
extern int sal_video_mask;
static FILE *ymlog;
static uint32_t ymlog_base; /* samples of the frames before this one */
static void ymlog_write(int a0, uint8_t value, int sample)
{
    fprintf(ymlog, "%u %d %u\n", ymlog_base + (uint32_t)sample, a0, value);
}

/* --profile: the main CPU's instructions per ROM page and per handler, from
 * Musashi's instruction hook (SAL_M68K_PROFILE, m68kconf.h). Not in the
 * build with the upstream core (build_ref.sh), which has no such tables. */
#if SAL_HOST_REF
#define SAL_M68K_NHANDLERS 1
static const char *const sal_m68k_names[1] = {"?"};
static const uint16_t sal_m68k_index[1];
#endif
static int profiling;
static uint32_t page_count[128];
static uint32_t handler_count[SAL_M68K_NHANDLERS];
extern const uint16_t *sal_fetch_page[256];
void sal_m68k_profile_hook(unsigned int pc)
{
    if (!profiling)
        return;
    pc &= 0xffffff;
    if (pc < 0x80000)
        page_count[pc >> 12]++;
#if !SAL_HOST_REF
    const unsigned op = sal_fetch_page[(pc >> 12) & 0xff][(pc & 0xfff) >> 1];
    handler_count[sal_m68k_index[op]]++;
#endif
}

static int cmp_desc(const void *a, const void *b, const uint32_t *counts)
{
    const uint32_t ca = counts[*(const int *)a], cb = counts[*(const int *)b];
    return ca < cb ? 1 : ca > cb ? -1 : 0;
}
static int cmp_pages(const void *a, const void *b)
{
    return cmp_desc(a, b, page_count);
}
static int cmp_handlers(const void *a, const void *b)
{
    return cmp_desc(a, b, handler_count);
}

static void write_profile(const char *base)
{
    char path[1100];
    int idx[SAL_M68K_NHANDLERS > 128 ? SAL_M68K_NHANDLERS : 128];
    uint64_t total = 0;
    for (int i = 0; i < 128; i++)
    {
        idx[i] = i;
        total += page_count[i];
    }
    qsort(idx, 128, sizeof(int), cmp_pages);
    snprintf(path, sizeof(path), "%s.pages", base);
    FILE *f = fopen(path, "w");
    if (f)
    {
        fprintf(f, "# instructions fetched per 4 KB page of the ROM, most first: page count share\n");
        uint64_t acc = 0;
        for (int i = 0; i < 128 && page_count[idx[i]]; i++)
        {
            acc += page_count[idx[i]];
            fprintf(f, "%3d %10u %5.1f%% (%5.1f%% so far)\n", idx[i], page_count[idx[i]],
                    100.0 * page_count[idx[i]] / (double)total, 100.0 * acc / (double)total);
        }
        fclose(f);
    }
    for (int i = 0; i < SAL_M68K_NHANDLERS; i++)
        idx[i] = i;
    qsort(idx, SAL_M68K_NHANDLERS, sizeof(int), cmp_handlers);
    snprintf(path, sizeof(path), "%s.handlers", base);
    f = fopen(path, "w");
    if (f)
    {
        fprintf(f, "# instructions per handler of the 68000 core, most first (for tools/m68k_dispatch.py --hot)\n");
        uint64_t acc = 0;
        for (int i = 0; i < SAL_M68K_NHANDLERS && handler_count[idx[i]]; i++)
        {
            acc += handler_count[idx[i]];
            fprintf(f, "%-32s # %10u %5.1f%% (%5.1f%% so far)\n", sal_m68k_names[idx[i]], handler_count[idx[i]],
                    100.0 * handler_count[idx[i]] / (double)total, 100.0 * acc / (double)total);
        }
        fclose(f);
    }
    printf("profile: %llu instructions, %s.pages and %s.handlers written\n", (unsigned long long)total, base, base);
}

/* The machine's data is kept until the end: no leak reports for it. */
const char *__asan_default_options(void)
{
    return "detect_leaks=0";
}

/* --- stdio implementation of sal_io_t -------------------------------------- */

static int host_list_dir(void *ctx, const char *dir, sal_dir_cb cb, void *arg)
{
    (void)ctx;
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
    {
        if (e->d_name[0] == '.')
            continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        cb(arg, e->d_name, (uint32_t)st.st_size, S_ISDIR(st.st_mode));
    }
    closedir(d);
    return 1;
}

static void *host_open(void *ctx, const char *path)
{
    (void)ctx;
    return fopen(path, "rb");
}

static uint32_t host_size(void *ctx, void *f)
{
    (void)ctx;
    long cur = ftell((FILE *)f);
    fseek((FILE *)f, 0, SEEK_END);
    long n = ftell((FILE *)f);
    fseek((FILE *)f, cur, SEEK_SET);
    return (uint32_t)n;
}

static int host_read_at(void *ctx, void *f, uint32_t ofs, void *buf, uint32_t len)
{
    (void)ctx;
    if (fseek((FILE *)f, ofs, SEEK_SET) != 0)
        return -1;
    return (int)fread(buf, 1, len, (FILE *)f);
}

static void host_close(void *ctx, void *f)
{
    (void)ctx;
    fclose((FILE *)f);
}

/* The loader's scratch memory, counted: the board's is PSRAM. */
static uint32_t scratch_now, scratch_peak;

static void *scratch_alloc(void *ctx, uint32_t size)
{
    (void)ctx;
    uint32_t *p = malloc(size + 8);
    p[0] = size;
    scratch_now += size;
    if (scratch_now > scratch_peak)
        scratch_peak = scratch_now;
    return p + 2;
}

static void scratch_free(void *ctx, void *p)
{
    (void)ctx;
    uint32_t *h = (uint32_t *)p - 2;
    scratch_now -= h[0];
    free(h);
}

static const sal_io_t io = {NULL,         host_list_dir, host_open,     host_size,
                            host_read_at, host_close,    scratch_alloc, scratch_free};

static void *host_alloc(uint32_t size)
{
    return malloc(size);
}

/* The board's SRAM for what the core keeps there: counted, so the harness
 * reports what the board would need. */
static uint32_t fast_total;
static void *fast_alloc(uint32_t size)
{
    fast_total += size;
    return malloc(size);
}

/* --- output helpers ------------------------------------------------------ */

static uint32_t rgb565(uint16_t p)
{
    return (uint32_t)(((p >> 11) & 0x1f) * 255 / 31) << 16 | (uint32_t)(((p >> 5) & 0x3f) * 255 / 63) << 8 |
           (uint32_t)((p & 0x1f) * 255 / 31);
}

static void to_rgb32(const uint16_t *fb, uint32_t *out)
{
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++)
        out[i] = rgb565(fb[i]);
}

static void put_rgb(FILE *f, const uint16_t *fb)
{
    static uint32_t px[CANVAS_W * CANVAS_H];
    static uint8_t rgb[CANVAS_W * CANVAS_H * 3];
    to_rgb32(fb, px);
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++)
    {
        rgb[3 * i] = (uint8_t)(px[i] >> 16);
        rgb[3 * i + 1] = (uint8_t)(px[i] >> 8);
        rgb[3 * i + 2] = (uint8_t)px[i];
    }
    fwrite(rgb, 3, CANVAS_W * CANVAS_H, f);
}

static void dump_frame(const char *outdir, int frame, const uint16_t *fb)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s/frame_%05d.ppm", outdir, frame);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", CANVAS_W, CANVAS_H);
    put_rgb(f, fb);
    fclose(f);
}

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

static void wav_header(FILE *f, uint32_t samples)
{
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + samples * 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 2);
    put32(f, SAMPLERATE);
    put32(f, SAMPLERATE * 4);
    put16(f, 4);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, samples * 4);
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int set_by_name(const char *s)
{
    for (int i = 0; i < SAL_SET_COUNT; i++)
        if (!strcasecmp(s, sal_sets[i].folder) || !strcasecmp(s, sal_sets[i].mameset))
            return i;
    return -1;
}

/* --- main ---------------------------------------------------------------- */

int main(int argc, char **argv)
{
    /* the positional arguments come before the options */
    const char *pos[5] = {NULL};
    int npos = 0, a = 1;
    for (; a < argc && argv[a][0] != '-'; a++)
    {
        if (npos == 5)
        {
            fprintf(stderr, "unexpected argument %s\n", argv[a]);
            return 1;
        }
        pos[npos++] = argv[a];
    }
    /* the set is optional: a number there is the frame count */
    if (npos >= 2 && isdigit((unsigned char)pos[1][0]) && set_by_name(pos[1]) < 0)
    {
        for (int i = npos; i > 1; i--)
            pos[i] = pos[i - 1];
        pos[1] = NULL;
        npos++;
    }
    int coin_at = -1, start_at = -1, play_at = -1, live = 0, scale = 0, mute = 0;
    int variant = -1, trace_at = -1, dumpram_at = -1;
    const char *video_path = NULL, *wav_path = NULL, *profile_path = NULL, *hash_path = NULL;
    int key_at[MAX_KEYS], nkeys = 0;
    uint32_t key_mask[MAX_KEYS];
    for (; a < argc; a++)
    {
        if (!strcmp(argv[a], "--live"))
            live = 1;
        else if (!strcmp(argv[a], "--scale") && a + 1 < argc)
            scale = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--mute"))
            mute = 1;
        else if (!strcmp(argv[a], "--variant") && a + 1 < argc)
            variant = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--wav") && a + 1 < argc)
            wav_path = argv[++a];
        else if (!strcmp(argv[a], "--coin") && a + 1 < argc)
            coin_at = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--start") && a + 1 < argc)
            start_at = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--play") && a + 1 < argc)
            play_at = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--video") && a + 1 < argc)
            video_path = argv[++a];
        else if (!strcmp(argv[a], "--profile") && a + 1 < argc)
            profile_path = argv[++a];
        else if (!strcmp(argv[a], "--hash") && a + 1 < argc)
            hash_path = argv[++a];
        else if (!strcmp(argv[a], "--only") && a + 1 < argc)
        {
            a++;
            sal_snd_mask = !strcmp(argv[a], "ym") ? 1 : !strcmp(argv[a], "pcm") ? 2 : !strcmp(argv[a], "vlm") ? 4 : 7;
        }
        else if (!strcmp(argv[a], "--ymlog") && a + 1 < argc)
        {
            ymlog = fopen(argv[++a], "w");
            if (ymlog)
                sal_ym_log = ymlog_write;
        }
        else if (!strcmp(argv[a], "--trace") && a + 1 < argc)
            trace_at = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--layers") && a + 1 < argc)
            sal_video_mask = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--dumpram") && a + 1 < argc)
            dumpram_at = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--keys") && a + 1 < argc && nkeys < MAX_KEYS)
        {
            if (sscanf(argv[++a], "%d:%x", &key_at[nkeys], &key_mask[nkeys]) == 2)
                nkeys++;
        }
        else
        {
            fprintf(stderr, "unknown option %s\n", argv[a]);
            return 1;
        }
    }
#if !HOST_SDL
    (void)scale;
    (void)mute;
    if (live)
    {
        fprintf(stderr,
                "%s: built without SDL2, so --live is not available.\n"
                "Install libsdl2-dev and rerun hosttest/build.sh.\n",
                argv[0]);
        return 1;
    }
#endif
    if (npos < (live ? 1 : 4))
    {
        fprintf(stderr,
                "usage: %s <romroot> [salamand|lifefrce] <frames> <dump-every-N> [outdir] [options]\n"
                "       %s <romroot> [salamand|lifefrce] [frames] [dump-every-N] [outdir] --live [options]\n",
                argv[0], argv[0]);
        return 1;
    }
    const char *romroot = pos[0];
    int set = pos[1] ? set_by_name(pos[1]) : -1;
    if (pos[1] && set < 0)
    {
        fprintf(stderr, "unknown set %s\n", pos[1]);
        return 1;
    }
    /* live: until the window is closed, frames dumped with F12 */
    const int frames = pos[2] ? atoi(pos[2]) : INT_MAX;
    const int every = pos[3] ? atoi(pos[3]) : 0;
    const char *outdir = pos[4] ? pos[4] : "hosttest/out";

    sal_init(SAL_FMT_RGB565, SAMPLERATE);

    /* what is on the "card" */
    char dir[1024];
    sal_files_t found[SAL_SET_COUNT];
    for (int s = 0; s < SAL_SET_COUNT; s++)
    {
        const sal_set_info_t *si = &sal_sets[s];
        snprintf(dir, sizeof(dir), "%s/%s", romroot, si->folder);
        found[s] = sal_probe((sal_set_id_t)s, &io, dir);
        const int v = sal_variant_found((sal_set_id_t)s, found[s]);
        printf("%-12s %2d of %2d files: %s%s\n", si->title, __builtin_popcountll(found[s]), si->nfiles,
               v >= 0 ? "complete, " : "incomplete", v >= 0 ? si->variant_set[v] : "");
        for (int w = 0; w < si->nvariants; w++)
            if (sal_variant_found((sal_set_id_t)s, found[s] & sal_variant_files((sal_set_id_t)s, w)) == w)
                printf("  variant %d %-10s (%s) complete\n", w, si->variant_set[w], si->variant_name[w]);
        if (set < 0 && v >= 0)
            set = s;
    }
    if (set < 0)
    {
        fprintf(stderr, "no complete set under %s\n", romroot);
        return 2;
    }
    const sal_set_info_t *si = &sal_sets[set];
    if (variant < 0)
        variant = sal_variant_found((sal_set_id_t)set, found[set]);
    if (variant < 0 || variant >= si->nvariants)
    {
        const int near = sal_variant_nearest((sal_set_id_t)set, found[set]);
        const sal_files_t missing = sal_variant_files((sal_set_id_t)set, near) & ~found[set];
        printf("%s is incomplete; for %s these are missing:\n", si->title, si->variant_set[near]);
        for (int i = 0; i < si->nfiles; i++)
            if (missing & ((sal_files_t)1 << i))
                printf("  %s (crc %08x)\n", si->files[i].name, si->files[i].crc);
        return 2;
    }

    snprintf(dir, sizeof(dir), "%s/%s", romroot, si->folder);
    uint8_t *parts[SAL_P_COUNT];
    double t0 = now_s();
    const sal_files_t got = sal_load((sal_set_id_t)set, variant, &io, dir, host_alloc, free, parts);
    if (got != sal_variant_files((sal_set_id_t)set, variant))
    {
        printf("%s (%s): loading failed\n", si->title, si->variant_set[variant]);
        return 2;
    }
    const double t_load = now_s() - t0;
    t0 = now_s();
    if (!sal_start((sal_set_id_t)set, variant, parts, fast_alloc))
    {
        printf("start failed\n");
        return 2;
    }
    printf("%s (%s, %s): loaded in %.0f ms, started in %.1f ms; %u bytes kept in SRAM\n", si->title,
           si->variant_set[variant], si->variant_name[variant], t_load * 1000, (now_s() - t0) * 1000,
           sal_fast_bytes());
    printf("ROM loader: at most %u bytes of scratch memory\n", scratch_peak);
    (void)fast_total;
    profiling = profile_path != NULL;

    mkdir(outdir, 0755);

    FILE *wav = NULL;
    if (wav_path)
    {
        wav = fopen(wav_path, "wb");
        if (wav)
            wav_header(wav, 0);
    }
    FILE *video = video_path ? fopen(video_path, "wb") : NULL;
    FILE *hash = hash_path ? fopen(hash_path, "w") : NULL;

    static uint16_t fb[CANVAS_W * CANVAS_H];
    int16_t audio[2 * SAMPLES_PER_FRAME];
    uint32_t total_samples = 0;
    double t_emu = 0, t_snd = 0, t_render = 0, t_emu_max = 0, t_snd_max = 0, t_render_max = 0;
    int peak = 0;

#if HOST_SDL
    static uint32_t px[CANVAS_W * CANVAS_H];
    if (live)
    {
        if (!hsdl_init(CANVAS_W, CANVAS_H, 0, scale ? scale : 3, !mute, 16667))
            return 1;
    }
#endif

    sal_pads_t pads;
    sal_pads_reset(&pads);
    pads.wait_release = 0;
    uint32_t pad = 0; /* live: the keyboard */
    int quit = 0;
    int max_events = 0; /* the most events in a frame (SAL_MAX_EVENTS) */
    for (int f = 0; f < frames && !quit; f++)
    {
        int settings = 0, fps = 0;
        uint32_t in = sal_pads_map(&pads, pad, 0, 0, &settings, &fps);
        if (settings)
        {
            printf("frame %d: SELECT+START: reset\n", f);
            sal_reset();
            sal_sound_reset();
            sal_pads_reset(&pads);
        }
        if (coin_at >= 0 && f >= coin_at && f < coin_at + 6)
            in |= SAL_IN_COIN1;
        if (start_at >= 0 && f >= start_at && f < start_at + 6)
            in |= SAL_IN_START1;
        for (int k = 0; k < nkeys; k++)
            if (f >= key_at[k] && f < key_at[k] + 6)
                in |= key_mask[k];
        if (play_at >= 0 && f >= play_at)
        {
            static const uint32_t dirs[4] = {SAL_IN_LEFT, SAL_IN_UP, SAL_IN_RIGHT, SAL_IN_DOWN};
            const int p = f - play_at;
            if ((p & 15) < 4)
                in |= SAL_IN_B2 | SAL_IN_B3;
            if ((p % 600) < 4)
                in |= SAL_IN_B1;
            in |= dirs[(p / 45) & 3];
        }

        double ta = now_s();
        sal_run_frame(in);
        double tb = now_s();
        const sal_event_t *ev;
        const int nev = sal_take_events(&ev);
        if (nev > max_events)
            max_events = nev;
        sal_sound_frame(ev, nev, audio, SAMPLES_PER_FRAME);
        ymlog_base += SAMPLES_PER_FRAME;
        double tc = now_s();
        sal_render_begin();
        for (int y = 0; y < SAL_H; y += 16) /* in bands, as the board draws */
            sal_render_rows(fb + (PIC_Y + y) * CANVAS_W + PIC_X, CANVAS_W, y, y + 16);
        double td = now_s();
        t_emu += tb - ta;
        t_snd += tc - tb;
        t_render += td - tc;
        if (tb - ta > t_emu_max)
            t_emu_max = tb - ta;
        if (tc - tb > t_snd_max)
            t_snd_max = tc - tb;
        if (td - tc > t_render_max)
            t_render_max = td - tc;

        for (int i = 0; i < 2 * SAMPLES_PER_FRAME; i++)
        {
            int v = audio[i] < 0 ? -audio[i] : audio[i];
            if (v > peak)
                peak = v;
        }
        if (wav)
        {
            fwrite(audio, sizeof(int16_t), 2 * SAMPLES_PER_FRAME, wav);
            total_samples += SAMPLES_PER_FRAME;
        }
        if (video)
            put_rgb(video, fb);
        if (every > 0 && (f % every) == 0)
            dump_frame(outdir, f, fb);

#if HOST_SDL
        if (live)
        {
            to_rgb32(fb, px);
            hsdl_present(px);
            for (;;)
            {
                int evs = 0;
                pad = hsdl_poll(&evs);
                if (evs & HSDL_EV_DUMP)
                {
                    dump_frame(outdir, f, fb);
                    printf("LIVE: frame %d dumped to %s/frame_%05d.ppm\n", f, outdir, f);
                }
                if (evs & HSDL_EV_QUIT)
                {
                    quit = 1;
                    break;
                }
                hsdl_status(si->title, f);
                if (!hsdl_paused() || (evs & HSDL_EV_STEP))
                    break;
                hsdl_idle();
            }
            if (!quit)
            {
                hsdl_audio(audio, SAMPLES_PER_FRAME);
                hsdl_pace();
            }
        }
#endif
        if (hash)
        {
            uint32_t regs[18];
            for (int r = 0; r < 16; r++)
                regs[r] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + r));
            regs[16] = m68k_get_reg(NULL, M68K_REG_PC);
            regs[17] = m68k_get_reg(NULL, M68K_REG_SR);
            uint32_t c = (uint32_t)mz_crc32(MZ_CRC32_INIT, (const uint8_t *)regs, sizeof(regs));
            c = (uint32_t)mz_crc32(c, (const uint8_t *)m.ram, sizeof(m.ram));
            c = (uint32_t)mz_crc32(c, (const uint8_t *)m.vram, sizeof(m.vram));
            c = (uint32_t)mz_crc32(c, (const uint8_t *)m.spriteram, sizeof(m.spriteram));
            fprintf(hash, "%d %08x %06x\n", f, c, regs[16]);
        }
        if (f == dumpram_at)
        {
            static const struct
            {
                const char *name;
                const void *p;
                size_t n;
            } dumps[] = {{"vram", m.vram, sizeof(m.vram)},     {"charram", m.charram, sizeof(m.charram)},
                         {"spriteram", m.spriteram, sizeof(m.spriteram)}, {"scroll", m.scroll, sizeof(m.scroll)},
                         {"palette", m.palette, sizeof(m.palette)}};
            for (size_t i = 0; i < sizeof(dumps) / sizeof(dumps[0]); i++)
            {
                char path[1100];
                snprintf(path, sizeof(path), "%s/ram_%d_%s.bin", outdir, f, dumps[i].name);
                FILE *df = fopen(path, "wb");
                if (df)
                {
                    fwrite(dumps[i].p, 1, dumps[i].n, df);
                    fclose(df);
                }
            }
            printf("frame %d: RAM dumped to %s/ram_%d_*.bin\n", f, outdir, f);
        }
        if (trace_at >= 0 && f >= trace_at && f < trace_at + 60)
        {
            const sal_stats_t *st = sal_stats();
            printf("trace frame %d: main pc %06x, sound pc %04x\n", f, st->main_pc, st->sound_pc);
        }
        if ((f + 1) % 60 == 0)
        {
            const sal_stats_t *st = sal_stats();
            printf("frame %5d  main %.2f ms (max %.2f)  sound %.2f (max %.2f)  render %.2f (max %.2f)  peak %5d  "
                   "pc %06x/%04x  latch %u irq %u  sprites %u  idle %u\n",
                   f + 1, t_emu * 1000 / 60, t_emu_max * 1000, t_snd * 1000 / 60, t_snd_max * 1000,
                   t_render * 1000 / 60, t_render_max * 1000, peak, st->main_pc, st->sound_pc, st->latch_writes,
                   st->sound_irqs, st->sprites_drawn, st->sound_idle_cycles);
#if SAL_FRAME_STATS
            printf("  parts us a frame: main %u, sound board %u (YM %u, PCM+VLM %u), draw %u (chars %u, sprites %u)\n",
                   sal_prof[0] / 60000, sal_prof[1] / 60000, sal_prof[2] / 60000, sal_prof[3] / 60000,
                   sal_prof[4] / 60000, sal_prof[5] / 60000, sal_prof[6] / 60000);
            memset(sal_prof, 0, sizeof(sal_prof));
#endif
            t_emu = t_snd = t_render = t_emu_max = t_snd_max = t_render_max = 0;
            peak = 0;
        }
    }

    printf("most events in a frame: %d (room for %d)\n", max_events, SAL_MAX_EVENTS);
    if (profile_path)
        write_profile(profile_path);
    if (wav)
    {
        wav_header(wav, total_samples);
        fclose(wav);
    }
    if (video)
        fclose(video);
    if (hash)
        fclose(hash);
    if (ymlog)
        fclose(ymlog);
#if HOST_SDL
    if (live)
        hsdl_quit();
#endif
    return 0;
}
