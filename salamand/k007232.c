/*
 * Konami 007232 PCM controller; see k007232.h.
 */
#include "k007232.h"

#include "sal_port.h"

#include <string.h>

void k007232_init(k007232_t *k, const uint8_t *rom, uint32_t rom_size, uint32_t clock, uint32_t samplerate)
{
    memset(k, 0, sizeof(*k));
    k->rom = rom;
    k->rom_mask = rom_size - 1;
    k->rate_step = (uint32_t)(((uint64_t)(clock / 128) << 16) / samplerate);
    k007232_reset(k);
}

void k007232_reset(k007232_t *k)
{
    for (int i = 0; i < 2; i++)
    {
        k007232_channel_t *c = &k->ch[i];
        c->addr = c->start = 0;
        c->counter = 0x1000;
        c->step = 0;
        c->play = 0;
    }
    /* channel A to output A, B to B, as the chip powers up */
    k->ch[0].vol[0] = 255;
    k->ch[0].vol[1] = 0;
    k->ch[1].vol[0] = 0;
    k->ch[1].vol[1] = 255;
    memset(k->wreg, 0, sizeof(k->wreg));
    k->rate_acc = 0;
    k->hold_a = k->hold_b = 0;
}

static void SAL_HOT(start)(k007232_t *k, int ch)
{
    k007232_channel_t *c = &k->ch[ch];
    if (c->start <= k->rom_mask)
    {
        c->play = 1;
        c->addr = c->start;
        c->counter = 0x1000;
    }
}

void SAL_HOT(k007232_write)(k007232_t *k, int reg, uint8_t data)
{
    if (reg < 0 || reg > 13)
        return;
    k->wreg[reg] = data;
    if (reg >= 12)
        return; /* 12: the external port (volume), 13: the loop flags */
    const int base = reg >= 6 ? 6 : 0;
    k007232_channel_t *c = &k->ch[reg >= 6 ? 1 : 0];
    switch (reg - base)
    {
    case 0: /* address step, LSB */
    case 1: /* address step, MSB (bits 4-5 would divide the clock: not emulated, as in MAME) */
        c->step = ((k->wreg[base + 1] & 0x0f) << 8) | k->wreg[base];
        break;
    case 2:
    case 3:
    case 4: /* the start address */
        c->start = ((uint32_t)(k->wreg[base + 4] & 1) << 16) | ((uint32_t)k->wreg[base + 3] << 8) | k->wreg[base + 2];
        break;
    case 5: /* start */
        start(k, base ? 1 : 0);
        break;
    }
}

uint8_t SAL_HOT(k007232_read)(k007232_t *k, int reg)
{
    if (reg == 5 || reg == 11)
        start(k, reg == 11 ? 1 : 0);
    return 0;
}

void k007232_set_volume(k007232_t *k, int channel, int vol_a, int vol_b)
{
    k->ch[channel].vol[0] = (uint8_t)vol_a;
    k->ch[channel].vol[1] = (uint8_t)vol_b;
}

/* One sample of the chip: the two channels summed into the two outputs. */
static inline void SAL_HOT(chip_sample)(k007232_t *k, int32_t *a, int32_t *b)
{
    int32_t asum = 0, bsum = 0;
    for (int i = 0; i < 2; i++)
    {
        k007232_channel_t *c = &k->ch[i];
        if (!c->play)
            continue;
        uint32_t addr = c->addr & 0x1ffff;
        while (c->counter <= c->step) /* rate: clock / (4 * (4096 - step)) */
        {
            if (addr > k->rom_mask || (k->rom[addr++ & k->rom_mask] & 0x80))
            {
                /* the end of the sample */
                if (k->wreg[13] & (1 << i))
                {
                    addr = c->start; /* loops */
                }
                else
                {
                    c->play = 0;
                    break;
                }
            }
            c->counter += 0x1000 - c->step;
        }
        c->addr = addr;
        if (!c->play)
            continue;
        c->counter -= 32;
        const int32_t out = (k->rom[addr & k->rom_mask] & 0x7f) - 0x40;
        asum += out * (c->vol[0] * 2);
        bsum += out * (c->vol[1] * 2);
    }
    *a = asum;
    *b = bsum;
}

void SAL_HOT(k007232_render)(k007232_t *k, int32_t *a, int32_t *b, int n)
{
    for (int i = 0; i < n; i++)
    {
        k->rate_acc += k->rate_step;
        while (k->rate_acc >= 0x10000)
        {
            k->rate_acc -= 0x10000;
            chip_sample(k, &k->hold_a, &k->hold_b);
        }
        a[i] += k->hold_a;
        b[i] += k->hold_b;
    }
}
