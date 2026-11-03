/*
 * The 68000's bus: what Musashi reads and writes, inline, from the machine's
 * RAM (sal_internal.h) and the ROM pages. Included by m68kconf.h, so every
 * file of Musashi sees it.
 *
 * Instruction fetches and ROM reads go through sal_fetch_page: one pointer
 * per 4 KB page of the CPU's first megabyte, to the page's copy in SRAM (the
 * pages read most, sal.c), to the ROM image (PSRAM or flash on the board), to the work
 * RAM, or to a page of zeros for what is not mapped. The RAM is 16-bit words
 * in the host's byte order, so a byte at address A is at byte A ^ 1 of its
 * word (both the host and the board are little-endian).
 */
#ifndef SAL_BUS_H
#define SAL_BUS_H

#include <stdint.h>

#include "sal_internal.h"

extern const uint16_t *sal_fetch_page[256];

/* sal.c: the interrupt acknowledge (releases the line), and the slow side
 * of the bus: the palette, the latches, the inputs and what is unmapped */
int sal_m68k_int_ack(int level);
unsigned sal_m68k_read16_slow(unsigned a);
void sal_m68k_write16_slow(unsigned a, unsigned v);
void sal_m68k_write8_slow(unsigned a, unsigned v);

static inline unsigned sal_m68k_fetch16(unsigned a)
{
    return sal_fetch_page[(a >> 12) & 0xff][(a & 0xfff) >> 1];
}

static inline unsigned sal_m68k_fetch32(unsigned a)
{
    return (sal_m68k_fetch16(a) << 16) | sal_m68k_fetch16(a + 2);
}

/* The word of RAM at address a (above the ROM), or NULL. */
static inline uint16_t *sal_m68k_ram_word(unsigned a)
{
    switch (a >> 16)
    {
    case 0x08:
        return &m.ram[(a & 0x7fff) >> 1];
    case 0x12:
        return &m.charram[(a & 0xffff) >> 1];
    case 0x10:
        return &m.vram[(a & 0x3fff) >> 1];
    case 0x19:
        return &m.scroll[(a & 0x1fff) >> 1];
    case 0x18:
        return &m.spriteram[(a & 0xfff) >> 1];
    default:
        return NULL;
    }
}

static inline unsigned sal_m68k_read16(unsigned a)
{
    a &= 0xffffff;
    if (a < 0x80000)
        return sal_fetch_page[a >> 12][(a & 0xfff) >> 1];
    const uint16_t *p = sal_m68k_ram_word(a);
    if (p)
        return *p;
    return sal_m68k_read16_slow(a);
}

static inline unsigned sal_m68k_read8(unsigned a)
{
    a &= 0xffffff;
    if (a < 0x80000)
        return ((const uint8_t *)sal_fetch_page[a >> 12])[(a & 0xfff) ^ 1];
    const uint16_t *p = sal_m68k_ram_word(a);
    if (p)
        return ((const uint8_t *)p)[(a & 1) ^ 1];
    const unsigned v = sal_m68k_read16_slow(a & ~1u);
    return (a & 1) ? (v & 0xff) : (v >> 8);
}

static inline unsigned sal_m68k_read32(unsigned a)
{
    return (sal_m68k_read16(a) << 16) | sal_m68k_read16(a + 2);
}

static inline void sal_m68k_write16(unsigned a, unsigned v)
{
    a &= 0xffffff;
    uint16_t *p = sal_m68k_ram_word(a);
    if (p)
    {
        *p = (uint16_t)v;
        return;
    }
    sal_m68k_write16_slow(a, v);
}

static inline void sal_m68k_write8(unsigned a, unsigned v)
{
    a &= 0xffffff;
    uint16_t *p = sal_m68k_ram_word(a);
    if (p)
    {
        ((uint8_t *)p)[(a & 1) ^ 1] = (uint8_t)v;
        return;
    }
    sal_m68k_write8_slow(a, v);
}

static inline void sal_m68k_write32(unsigned a, unsigned v)
{
    sal_m68k_write16(a, v >> 16);
    sal_m68k_write16(a + 2, v & 0xffff);
}

#endif
