/*
 * Salamander: the video, after MAME's konami/nemesis_v.cpp (license
 * BSD-3-Clause, copyright-holders Bryan McPhail).
 *
 * Two layers of 8x8 characters from 64x32 maps, each scrolled in X per raster
 * line and in Y per 8-pixel column of the map; the characters, and the
 * sprites, are 4-bit pixels in the 64 KB of character RAM, two to a byte,
 * the first in the high nibble, the bytes in the 68000's order (the even
 * address first). A map word gives the character (bits 0-10),
 * flip Y (bit 11), and the order the two layers are drawn in: with bit 12
 * ("mask") or bit 14 ("layer") set, the character is drawn after the plain
 * ones, and with bit 12 set it also hides sprites. A character with bit 13
 * clear, or bits 14-15 = 01, is opaque; otherwise pen 0 is transparent. Its
 * colour word gives the palette (bits 0-6, of 16 colours each) and flip X
 * (bit 7). Each raster line is drawn in MAME's order: layer 1 then layer 0 of
 * the plain characters, then layer 1 then layer 0 of the rest, where the
 * sprite-hiding ones (bit 12) come after the others within a layer.
 *
 * Sprites: 256 of 8 words (priority, size and flip X, zoom, code, colour and
 * flip Y, X, Y) in 8 sizes from 8x8 to 64x64, zoomed by a 10-bit factor
 * (0x80 = unzoomed), drawn from priority 255 down to 0 and within a priority
 * from the last to the first, so the first wins; pen 0 is transparent, and a
 * sprite pixel is hidden where a sprite-hiding character pixel was drawn.
 *
 * Colours: 2048 words of xBGR555 through the board's resistor ladders, in
 * the canvas's format. Flip screen (a cocktail cabinet) is not drawn.
 *
 * The picture is drawn in bands of rows: each row's characters first, into
 * the canvas, then the sprites that cross the band.
 */
#include "sal_internal.h"

/* the picture's rows are raster lines 16..239 */
#define RASTER_Y0 SAL_FIRST_LINE

/* the scroll RAM as the game lays it out (sal.c) */
#define XSCROLL(n) (&m.scroll[(n) ? 0 : 0x200])
#define YSCROLL(n) (&m.scroll[(n) ? 0x7c0 : 0x780])
#define VIDEORAM(n) (&m.vram[(n) ? 0 : 0x800])
#define COLORRAM(n) (&m.vram[(n) ? 0x1000 : 0x1800])

static uint16_t SAL_RAM_DATA pal16[2048]; /* the palette in the canvas's format */

/* The host harness's choice of what is drawn (--layers): bit 0 the
 * characters, bit 1 the sprites. */
int sal_video_mask = 3;
static uint8_t ramp[32];                   /* a 5-bit gun through the resistor ladder */

/* ---------------------------------------------------------------------------
 * Colours
 * ------------------------------------------------------------------------ */

/* MAME's compute_res_net for the board's ladders (nemesis_v.cpp): 74LS09 open
 * collector drivers, 4700, 2400, 1200, 620 and 300 ohms to the 5 bits, a
 * 1 kohm bias to 5 V, an emitter follower (0.7 V dropped), and the result
 * normalised so that 0 is black and 31 white. */
static void make_ramp(void)
{
    static const double R[5] = {4700, 2400, 1200, 620, 300};
    double raw[32];
    for (int i = 0; i < 32; i++)
    {
        double rTotal = 0, v = 0;
        for (int b = 0; b < 5; b++)
        {
            if (!((i >> b) & 1))
            {
                rTotal += 1.0 / R[b];
                v += 0.05 / R[b]; /* TTL low */
            }
        }
        rTotal += 1.0 / 1000;
        v += 5.0 / 1000;
        rTotal = 1.0 / rTotal;
        v *= rTotal;
        v -= 0.7;
        if (v < 0)
            v = 0;
        raw[i] = (int)(v * 255 / 5 + 0.4);
    }
    const double black = raw[0];
    const double white = 255.0 / (raw[31] - black);
    for (int i = 0; i < 32; i++)
        ramp[i] = (uint8_t)(int)((raw[i] - black) * white + 0.5);
}

static inline uint16_t pack(uint8_t r, uint8_t g, uint8_t b)
{
    switch (m.fmt)
    {
    case SAL_FMT_RGB444:
        return (uint16_t)(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
    case SAL_FMT_RGB565:
        return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    default:
        return (uint16_t)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
    }
}

static void SAL_HOT(update_palette)(void)
{
    const uint8_t *p = m.palette;
    for (int i = 0; i < 2048; i++)
    {
        const unsigned data = (p[2 * i] << 8) | p[2 * i + 1]; /* big-endian, as MAME's palette device reads a 68000's bytes */
        pal16[i] = pack(ramp[data & 0x1f], ramp[(data >> 5) & 0x1f], ramp[(data >> 10) & 0x1f]);
    }
    m.palette_dirty = 0;
    m.stats.palette_rebuilds++;
}

void sal_video_init(void)
{
    make_ramp();
}

void sal_video_reset(void)
{
    m.palette_dirty = 1;
}

/* A column's character, decoded once for the rows of the frame that cross
 * it: a row changes a column's character once in eight rows. */
typedef struct
{
    uint16_t idx;    /* the map entry, 0xffff none */
    uint8_t kind;    /* 0xff: blank, nothing drawn */
    uint8_t opaque, flipx, flipy;
    const uint8_t *tile; /* its 32 bytes in the character RAM */
    const uint16_t *colors;
} column_t;

static column_t SAL_RAM_DATA columns[2][64];

/* ---------------------------------------------------------------------------
 * Sprites: the list of the frame
 * ------------------------------------------------------------------------ */

static const struct
{
    uint8_t width, height;
} sprite_data[8] = {{32, 32}, {16, 32}, {32, 16}, {64, 64}, {8, 8}, {16, 8}, {8, 16}, {16, 16}};

typedef struct
{
    int16_t sx, sy;      /* the top left, in the picture (y: raster line - 16) */
    uint16_t dstw, dsth; /* the size drawn */
    uint32_t dx, dy;     /* source pixels per drawn pixel, 16.16 */
    uint32_t base;       /* the pixels' first byte in the character RAM */
    uint8_t stride;      /* bytes per source row */
    uint8_t color;       /* x 16: the palette */
    uint8_t flipx, flipy;
} sprite_t;

static sprite_t sprites[256];
static uint8_t order[256]; /* the sprites in the order they are drawn */
static int nsprites;

static void SAL_HOT(prepare_sprites)(void)
{
    const uint16_t *sr = m.spriteram;
    uint8_t prio[256];
    int count[256];
    nsprites = 0;
    for (int i = 0; i < 256; i++)
        count[i] = 0;

    for (int address = 0; address < 0x800; address += 8)
    {
        const unsigned w1 = sr[address + 1], w2 = sr[address + 2], w3 = sr[address + 3], w4 = sr[address + 4];
        unsigned zoom = w2 & 0xff;
        unsigned code;
        if (!(w2 & 0xff00) && ((w3 & 0xff00) != 0xff00))
            code = w3 + ((w4 & 0xc0) << 2);
        else
            code = (w3 & 0xff) + ((w4 & 0xc0) << 2);
        if (zoom == 0xff && code == 0)
            continue;
        const unsigned size = w1;
        zoom += (size & 0xc0) << 2;
        if (!zoom)
            continue;
        int sx = sr[address + 5] & 0xff;
        const int sy = sr[address + 6] & 0xff;
        if (w4 & 1)
            sx -= 0x100; /* fixes left side clip */
        const unsigned idx = (size >> 3) & 7;
        const unsigned w = sprite_data[idx].width, h = sprite_data[idx].height;
        code = code * 8 * 16 / (w * h);
        const uint32_t base = code * (w * h / 2);
        if (base >= 0x10000)
            continue;
        zoom = ((1u << 16) * 0x80 / zoom) + 0x02ab;
        const unsigned dstw = (w * zoom + 0x8000) >> 16, dsth = (h * zoom + 0x8000) >> 16;
        if (!dstw || !dsth)
            continue;
        if (sx >= SAL_W || sx + (int)dstw <= 0 || sy + (int)dsth <= RASTER_Y0 || sy >= RASTER_Y0 + SAL_H)
            continue;
        sprite_t *s = &sprites[nsprites];
        s->sx = (int16_t)sx;
        s->sy = (int16_t)(sy - RASTER_Y0);
        s->dstw = (uint16_t)dstw;
        s->dsth = (uint16_t)dsth;
        s->dx = (w << 16) / dstw;
        s->dy = (h << 16) / dsth;
        s->base = base;
        s->stride = (uint8_t)(w / 2);
        s->color = (uint8_t)((w4 & 0x1e) >> 1);
        s->flipx = (uint8_t)(w1 & 1);
        s->flipy = (uint8_t)((w4 >> 5) & 1);
        prio[nsprites] = (uint8_t)(sr[address] & 0xff);
        count[prio[nsprites]]++;
        nsprites++;
    }
    /* drawn from priority 255 down, and within a priority from the last in
     * RAM to the first: a counting sort, filled from the back */
    int pos = 0;
    int start[256];
    for (int p = 255; p >= 0; p--)
    {
        start[p] = pos;
        pos += count[p];
    }
    for (int i = nsprites - 1; i >= 0; i--)
        order[start[prio[i]]++] = (uint8_t)i;
    m.stats.sprites_drawn = (uint32_t)nsprites;
}

void SAL_HOT(sal_render_begin)(void)
{
    if (m.set < 0)
        return;
    if (m.palette_dirty)
        update_palette();
    prepare_sprites();
    /* the columns' characters are decoded afresh: the game has drawn */
    for (int n = 0; n < 2; n++)
        for (int c = 0; c < 64; c++)
            columns[n][c].idx = 0xffff;
}

/* ---------------------------------------------------------------------------
 * The characters of a row
 * ------------------------------------------------------------------------ */

/* A character of a row: its 8 pixels (pixel k in bits 4k..4k+3), where it
 * starts and its palette. */
typedef struct
{
    uint32_t pix;
    const uint16_t *colors;
    int16_t x0;
    uint8_t opaque; /* pen 0 drawn too */
} seg_t;

/* The characters a row crosses, by kind: 0 plain, 1 drawn later, 2 drawn
 * later and hiding sprites (MAME's categories 0, 2 and 3). */
typedef struct
{
    seg_t seg[33];
    uint8_t list[3][33]; /* the segments of each kind */
    uint8_t n[3];
} row_t;

/* bit x of a row's pri2: a sprite-hiding pixel was drawn there */
static uint32_t SAL_RAM_DATA pri2[SAL_H][8];

/* The characters of layer n crossing raster line L, left to right. */
static void SAL_HOT(row_segments)(int n, int L, row_t *r)
{
    const uint16_t *xs = XSCROLL(n), *ys = YSCROLL(n);
    const uint16_t *vr = VIDEORAM(n), *cr = COLORRAM(n);
    const uint8_t *chars = (const uint8_t *)m.charram;
    column_t *cols = columns[n];
    const int scrollx = (xs[L] & 0xff) | ((xs[0x100 + L] & 1) << 8);
    const int c0 = scrollx >> 3, ox = scrollx & 7;
    const int ncols = ox ? 33 : 32;
    r->n[0] = r->n[1] = r->n[2] = 0;
    for (int k = 0; k < ncols; k++)
    {
        const int c = (c0 + k) & 63;
        const int ty = (L + ys[c]) & 255;
        const unsigned idx = (unsigned)((ty >> 3) * 64 + c);
        column_t *t = &cols[c];
        if (t->idx != idx)
        {
            const unsigned code = vr[idx], color = cr[idx];
            t->idx = (uint16_t)idx;
            if (!(code & 0xf800))
            {
                t->kind = 0xff; /* blank: nothing drawn */
                continue;
            }
            t->tile = chars + (code & 0x7ff) * 32;
            t->colors = &pal16[(color & 0x7f) * 16];
            t->flipx = (uint8_t)((color >> 7) & 1);
            t->flipy = (uint8_t)((code >> 11) & 1);
            t->opaque = (uint8_t)(!(code & 0x2000) || ((code & 0xc000) == 0x4000));
            const unsigned mask = (code >> 12) & 1;
            unsigned layer = (code >> 14) & 1;
            if (mask)
                layer = 1;
            t->kind = (uint8_t)(mask + layer); /* categories 0, 2, 3 as 0, 1, 2 */
        }
        else if (t->kind == 0xff)
        {
            continue;
        }
        int row = ty & 7;
        if (t->flipy)
            row = 7 - row;
        uint32_t pix;
        memcpy(&pix, t->tile + row * 4, 4);
        /* The row is two of the 68000's words, the first pixel in the high
         * nibble of each word's high byte: pixels 0-3 are the nibbles of the
         * low half of the little-endian value from its bit 12 down, pixels
         * 4-7 those of the high half. Pixel k goes to bits 4k..4k+3: the
         * halves' bytes swapped, then the nibbles of each byte. Flipped in X,
         * pixel k is pixel 7 - k: the halves swapped. */
        if (t->flipx)
        {
            pix = (pix >> 16) | (pix << 16);
        }
        else
        {
            pix = ((pix & 0x00ff00ffu) << 8) | ((pix >> 8) & 0x00ff00ffu);
            pix = ((pix >> 4) & 0x0f0f0f0fu) | ((pix & 0x0f0f0f0fu) << 4);
        }
        if (!pix && !t->opaque)
            continue; /* all transparent */
        seg_t *s = &r->seg[k];
        s->pix = pix;
        s->colors = t->colors;
        s->x0 = (int16_t)(k * 8 - ox);
        s->opaque = t->opaque;
        r->list[t->kind][r->n[t->kind]++] = (uint8_t)k;
    }
}

/* Draws the characters of one kind of a row; `hide` marks their pixels as
 * hiding sprites. */
static void SAL_HOT(draw_segments)(uint16_t *dst, uint32_t *pri, const row_t *r, int kind, int hide)
{
    const uint8_t *list = r->list[kind];
    const int n = r->n[kind];
    for (int i = 0; i < n; i++)
    {
        const seg_t *s = &r->seg[list[i]];
        const uint16_t *c = s->colors;
        uint32_t pix = s->pix;
        const int x0 = s->x0;
        uint32_t shown; /* bit i: pixel x0 + i drawn */
        if (x0 >= 0 && x0 + 8 <= SAL_W)
        {
            uint16_t *d = dst + x0;
            if (s->opaque)
            {
                d[0] = c[pix & 15];
                d[1] = c[(pix >> 4) & 15];
                d[2] = c[(pix >> 8) & 15];
                d[3] = c[(pix >> 12) & 15];
                d[4] = c[(pix >> 16) & 15];
                d[5] = c[(pix >> 20) & 15];
                d[6] = c[(pix >> 24) & 15];
                d[7] = c[pix >> 28];
                shown = 0xff;
            }
            else
            {
                shown = 0;
                uint32_t pen;
                if ((pen = pix & 15)) { d[0] = c[pen]; shown |= 0x01; }
                if ((pen = (pix >> 4) & 15)) { d[1] = c[pen]; shown |= 0x02; }
                if ((pen = (pix >> 8) & 15)) { d[2] = c[pen]; shown |= 0x04; }
                if ((pen = (pix >> 12) & 15)) { d[3] = c[pen]; shown |= 0x08; }
                if ((pen = (pix >> 16) & 15)) { d[4] = c[pen]; shown |= 0x10; }
                if ((pen = (pix >> 20) & 15)) { d[5] = c[pen]; shown |= 0x20; }
                if ((pen = (pix >> 24) & 15)) { d[6] = c[pen]; shown |= 0x40; }
                if ((pen = pix >> 28)) { d[7] = c[pen]; shown |= 0x80; }
            }
        }
        else
        {
            /* at an edge: clipped */
            shown = 0;
            for (int i2 = 0; i2 < 8; i2++, pix >>= 4)
            {
                const int x = x0 + i2;
                if (x < 0 || x >= SAL_W)
                    continue;
                const uint32_t pen = pix & 15;
                if (pen || s->opaque)
                {
                    dst[x] = c[pen];
                    shown |= 1u << i2;
                }
            }
        }
        if (hide && shown)
        {
            /* the sprite-hiding pixels, x0 may be -7..255 */
            const int xa = x0 < 0 ? 0 : x0;
            const uint32_t bits = x0 < 0 ? shown >> -x0 : shown;
            pri[xa >> 5] |= bits << (xa & 31);
            if ((xa & 31) > 24 && xa + 8 <= SAL_W)
                pri[(xa >> 5) + 1] |= bits >> (32 - (xa & 31));
        }
    }
}

static void SAL_HOT(draw_row)(uint16_t *dst, int y)
{
    row_t r1, r0;
    const int L = y + RASTER_Y0;
    uint32_t *pri = pri2[y];
    for (int i = 0; i < 8; i++)
        pri[i] = 0;
    /* MAME: the bitmap is filled with pen 0 first */
    const uint16_t bg = pal16[0];
    for (int x = 0; x < SAL_W; x++)
        dst[x] = bg;
    if (!(sal_video_mask & 1))
        return;
    row_segments(1, L, &r1);
    row_segments(0, L, &r0);
    /* MAME's order: for i = 0, 2: layer 1 category i at priority 1, i + 1 at
     * priority 2, then layer 0 the same (category 1 never occurs) */
    draw_segments(dst, pri, &r1, 0, 0);
    draw_segments(dst, pri, &r0, 0, 0);
    draw_segments(dst, pri, &r1, 1, 0);
    draw_segments(dst, pri, &r1, 2, 1);
    draw_segments(dst, pri, &r0, 1, 0);
    draw_segments(dst, pri, &r0, 2, 1);
}

/* ---------------------------------------------------------------------------
 * The sprites crossing a band
 * ------------------------------------------------------------------------ */

static void SAL_HOT(draw_sprite)(const sprite_t *s, uint16_t *fb, int stride, int y0, int y1)
{
    const uint8_t *chars = (const uint8_t *)m.charram + s->base;
    const uint16_t *colors = &pal16[s->color * 16];
    int ry0 = s->sy, ry1 = s->sy + s->dsth;
    if (ry0 < y0)
        ry0 = y0;
    if (ry1 > y1)
        ry1 = y1;
    int i0 = 0, i1 = s->dstw;
    if (s->sx + i0 < 0)
        i0 = -s->sx;
    if (s->sx + i1 > SAL_W)
        i1 = SAL_W - s->sx;
    const uint32_t dx = s->dx, dy = s->dy;
    const int unzoomed = (dx == 0x10000);
    for (int ry = ry0; ry < ry1; ry++)
    {
        const int j = ry - s->sy;
        const uint32_t srow = ((uint32_t)(s->flipy ? (s->dsth - 1 - j) : j) * dy) >> 16;
        const uint8_t *src = chars + srow * s->stride;
        uint16_t *dst = fb + (ry - y0) * stride + s->sx;
        const uint32_t *pri = pri2[ry];
        if (unzoomed && !s->flipx)
        {
            for (int i = i0; i < i1; i++)
            {
                const unsigned b = src[(i >> 1) ^ 1]; /* the 68000's byte order, see row_segments */
                const unsigned pen = (i & 1) ? (b & 15) : (b >> 4);
                if (!pen)
                    continue;
                const int x = s->sx + i;
                if (pri[x >> 5] & (1u << (x & 31)))
                    continue;
                dst[i] = colors[pen];
            }
            continue;
        }
        for (int i = i0; i < i1; i++)
        {
            const uint32_t scol = ((uint32_t)(s->flipx ? (s->dstw - 1 - i) : i) * dx) >> 16;
            const unsigned b = src[(scol >> 1) ^ 1];
            const unsigned pen = (scol & 1) ? (b & 15) : (b >> 4);
            if (!pen)
                continue;
            const int x = s->sx + i;
            if (pri[x >> 5] & (1u << (x & 31)))
                continue;
            dst[i] = colors[pen];
        }
    }
}

void SAL_HOT(sal_render_rows)(uint16_t *fb, int stride, int y0, int y1)
{
    if (m.set < 0)
        return;
    SAL_PROF_START(t0);
    for (int y = y0; y < y1; y++)
        draw_row(fb + (y - y0) * stride, y);
    SAL_PROF_ADD(5, t0);
    SAL_PROF_START(t1);
    for (int k = 0; k < nsprites && (sal_video_mask & 2); k++)
    {
        const sprite_t *s = &sprites[order[k]];
        if (s->sy >= y1 || s->sy + s->dsth <= y0)
            continue;
        draw_sprite(s, fb, stride, y0, y1);
    }
    SAL_PROF_ADD(6, t1);
    SAL_PROF_ADD(4, t0);
}
