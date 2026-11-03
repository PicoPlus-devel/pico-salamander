/*
 * Salamander: the flash store, for boards without PSRAM. Modeled on pico-1943's
 * flash store (c1943_store.c).
 *
 * The machine's data (sal_part_t, 688 KB) does not fit in SRAM. On a board
 * without PSRAM it is built in flash once, from the ROM set on the card, and
 * read from there (XIP) after that, also without the card. A slot per set:
 *
 *   0x0000  header (one 4 KB sector): magic, version, set, variant, the size
 *           and CRC32 of the data
 *   0x1000  the parts, one after the other in sal_part_t order, exactly as
 *           sal_load builds them
 *
 * Two slots when the flash has room for both sets, else one, which the other
 * set replaces. The header is written last, so a copy cut short is never used;
 * the data is checked against its CRC once per boot.
 *
 * Copying erases and programs flash, so it must run with nothing else using
 * flash: on the board before the display starts. The loader hands over each
 * file whole, and the main CPU's ROMs are pairs of files of up to 128 KB, a byte
 * of each in turn, so a pair takes 256 KB of SRAM: one file in the buffer the
 * board gives (the framebuffer, not shown yet), the other in the machine's own
 * state `m`, which nothing uses while no set runs.
 */
#include "sal_store.h"

#include <stdio.h>
#include <string.h>

#include "miniz.h"
#include "sal_internal.h"

#define MAGIC 0x534c4153u /* "SALS" */
#define VERSION 1
#define SECTOR 4096
#define BLOCK 65536
#define HDR_SIZE SECTOR
#define MAX_FILE 0x20000 /* the largest ROM file */

#define BIT(i) ((sal_files_t)1 << (i))

_Static_assert(SAL_STORE_TEMP == MAX_FILE + SECTOR, "the buffer holds a file and a sector");
_Static_assert(sizeof(sal_t) >= MAX_FILE, "the machine's state holds the other file of a pair");

typedef struct
{
    uint32_t magic, version;
    uint32_t set, variant;
    uint32_t data_size, crc;
} hdr_t;

static sal_flash_t fl;
static int opened;
static int nslots;
static uint32_t part_ofs[SAL_P_COUNT], data_size, slot_size;
static int8_t checked[2]; /* 0 not yet, 1 good, -1 bad */

/* the parts' offsets in a slot, and the sizes of its data and of the slot */
static void layout(void)
{
    uint32_t o = HDR_SIZE;
    for (int p = 0; p < SAL_P_COUNT; p++)
    {
        part_ofs[p] = o;
        o += sal_part_size[p];
    }
    data_size = o - HDR_SIZE;
    slot_size = (o + BLOCK - 1) / BLOCK * BLOCK;
}

static const hdr_t *slot_hdr(int k)
{
    return (const hdr_t *)(fl.base + (uint32_t)k * slot_size);
}

/* The CRC does not cover the header, and the variant indexes the set's tables:
 * an unrelated sector must not get past this. */
static int hdr_valid(const hdr_t *h)
{
    return h->magic == MAGIC && h->version == VERSION && h->set < SAL_SET_COUNT &&
           h->variant < sal_sets[h->set].nvariants && h->data_size == data_size;
}

void sal_store_open(const sal_flash_t *flash)
{
    fl = *flash;
    layout();
    nslots = (int)(fl.size / slot_size);
    if (nslots > 2)
        nslots = 2;
    checked[0] = checked[1] = 0;
    opened = 1;
}

int sal_store_room(void)
{
    return opened && nslots > 0;
}

uint32_t sal_store_slot_size(void)
{
    layout();
    return slot_size;
}

int sal_store_slots(void)
{
    return opened ? nslots : 0;
}

/* the slot a set goes to: its own with two slots, else the only one */
static int slot_of(sal_set_id_t set)
{
    return nslots >= 2 ? (int)set : 0;
}

static int slot_good(int k)
{
    if (!checked[k])
    {
        const hdr_t *h = slot_hdr(k);
        checked[k] = -1;
        if (hdr_valid(h))
        {
            const uint32_t crc =
                (uint32_t)mz_crc32(MZ_CRC32_INIT, fl.base + (uint32_t)k * slot_size + HDR_SIZE, h->data_size);
            checked[k] = (crc == h->crc) ? 1 : -1;
        }
    }
    return checked[k] > 0;
}

int sal_store_variant(sal_set_id_t set)
{
    if (!sal_store_room())
        return -1;
    const int k = slot_of(set);
    const hdr_t *h = slot_hdr(k);
    if (!hdr_valid(h) || h->set != (uint32_t)set || !slot_good(k))
        return -1;
    return (int)h->variant;
}

int sal_store_parts(sal_set_id_t set, const uint8_t **parts)
{
    if (sal_store_variant(set) < 0)
        return 0;
    const uint8_t *slot = fl.base + (uint32_t)slot_of(set) * slot_size;
    for (int p = 0; p < SAL_P_COUNT; p++)
        parts[p] = slot + part_ofs[p];
    return 1;
}

/* ---------------------------------------------------------------------------
 * Copying
 * ------------------------------------------------------------------------ */

typedef struct
{
    sal_set_id_t set;
    int variant;
    const sal_set_info_t *s;
    sal_files_t want; /* the variant's files */
    const sal_io_t *io;
    const char *dir;
    uint8_t *buf;  /* MAX_FILE bytes */
    uint8_t *buf2; /* MAX_FILE bytes */
    uint8_t *out;  /* a sector */
    int file;      /* the file of a pair that goes in buf */
    uint32_t pos;  /* flash offset written up to */
    uint32_t crc;
    uint32_t since; /* bytes since copying() was last called */
} copy_t;

static void alive(copy_t *c, uint32_t bytes)
{
    /* a sign of life about every 64 KB (the board blinks its LED) */
    c->since += bytes;
    if (c->since >= BLOCK && fl.copying)
    {
        fl.copying((int)c->set);
        c->since = 0;
    }
}

/* len is a multiple of the page size */
static void put(copy_t *c, const uint8_t *data, uint32_t len)
{
    fl.program(c->pos, data, len);
    c->crc = (uint32_t)mz_crc32(c->crc, data, len);
    c->pos += len;
    alive(c, len);
}

static int wanted_in(const copy_t *c, int i, int region)
{
    return (c->want & BIT(i)) && c->s->files[i].region == region;
}

static uint8_t *dest_pair(void *arg, int i)
{
    const copy_t *c = (const copy_t *)arg;
    return i == c->file ? c->buf : c->buf2;
}

static uint8_t *dest_region(void *arg, int i)
{
    const copy_t *c = (const copy_t *)arg;
    return c->buf + c->s->files[i].offset;
}

/* The main CPU's ROM: the variant's pairs of files, each a byte of the file at
 * the even offset and a byte of the one at the odd offset, as sal_load puts
 * them (byte A of the CPU's ROM is byte A ^ 1 of the part). What no file
 * covers stays zero, also as there. */
static int copy_main(copy_t *c)
{
    const uint32_t size = sal_region_size[SAL_R_MAIN];
    uint32_t o = 0;
    while (o < size)
    {
        uint32_t next = size;
        for (int i = 0; i < c->s->nfiles; i++)
            if (wanted_in(c, i, SAL_R_MAIN))
            {
                const uint32_t start = c->s->files[i].offset & ~1u;
                if (start >= o && start < next)
                    next = start;
            }
        memset(c->out, 0, SECTOR);
        for (; o < next; o += SECTOR)
            put(c, c->out, SECTOR);
        if (o >= size)
            break;

        int even = -1, odd = -1;
        for (int i = 0; i < c->s->nfiles; i++)
            if (wanted_in(c, i, SAL_R_MAIN) && (c->s->files[i].offset & ~1u) == o)
            {
                if (c->s->files[i].offset & 1)
                    odd = i;
                else
                    even = i;
            }
        if (even < 0 || odd < 0)
            return 0;
        const uint32_t n = c->s->files[even].size;
        const sal_files_t need = BIT(even) | BIT(odd);
        c->file = even;
        if (sal_load_files(c->set, need, c->io, c->dir, dest_pair, c) != need)
            return 0;
        const uint8_t *e = c->buf, *d = c->buf2;
        for (uint32_t k = 0; k < n; k += SECTOR / 2)
        {
            for (uint32_t j = 0; j < SECTOR / 2; j++)
            {
                c->out[(2 * j) ^ 1] = e[k + j];
                c->out[(2 * j + 1) ^ 1] = d[k + j];
            }
            put(c, c->out, SECTOR);
        }
        o += 2 * n;
    }
    return 1;
}

/* A region of plain files, as they are; what no file covers stays zero. */
static int copy_region(copy_t *c, int r)
{
    sal_files_t need = 0;
    for (int i = 0; i < c->s->nfiles; i++)
        if (wanted_in(c, i, r))
            need |= BIT(i);
    memset(c->buf, 0, sal_region_size[r]);
    if (sal_load_files(c->set, need, c->io, c->dir, dest_region, c) != need)
        return 0;
    put(c, c->buf, sal_region_size[r]);
    return 1;
}

/* The parts, then the header. */
static int build_slot(copy_t *c, uint32_t slot)
{
    c->pos = fl.offset + slot + HDR_SIZE;
    c->crc = MZ_CRC32_INIT;
    if (!copy_main(c))
        return 0;
    for (int r = SAL_R_SOUND; r < SAL_R_COUNT; r++)
        if (!copy_region(c, r))
            return 0;

    memset(c->out, 0xff, SECTOR);
    hdr_t *h = (hdr_t *)c->out;
    h->magic = MAGIC;
    h->version = VERSION;
    h->set = (uint32_t)c->set;
    h->variant = (uint32_t)c->variant;
    h->data_size = data_size;
    h->crc = c->crc;
    fl.program(fl.offset + slot, c->out, SECTOR);
    return 1;
}

sal_files_t sal_store_copy(sal_set_id_t set, int v, const sal_io_t *io, const char *dir, uint8_t *temp,
                           uint32_t temp_size)
{
    if (!sal_store_room() || temp_size < SAL_STORE_TEMP)
        return 0;
    const sal_files_t want = sal_variant_files(set, v);
    /* first: is the set there at all */
    const sal_files_t found = sal_probe(set, io, dir) & want;
    if (found != want)
        return found;

    const int k = slot_of(set);
    const uint32_t slot = (uint32_t)k * slot_size;
    copy_t c;
    memset(&c, 0, sizeof(c));
    c.set = set;
    c.variant = v;
    c.s = &sal_sets[set];
    c.want = want;
    c.io = io;
    c.dir = dir;
    c.buf = temp;
    c.buf2 = (uint8_t *)&m;
    c.out = temp + MAX_FILE;

    if (fl.copying)
        fl.copying((int)set);
    for (uint32_t o = 0; o < slot_size; o += BLOCK)
    {
        fl.erase(fl.offset + slot + o, BLOCK);
        alive(&c, BLOCK);
    }
    checked[k] = 0;

    /* `m` holds a ROM file from here on: back to what sal_init left, whatever
     * becomes of the copy */
    const sal_fmt_t fmt = m.fmt;
    const int samplerate = m.samplerate;
    const int ok = build_slot(&c, slot);
    memset(&m, 0, sizeof(m));
    sal_init(fmt, samplerate);

    checked[k] = 0;
    return ok ? want : 0;
}
