/*
 * The controllers onto the cabinet; see sal_input.h. After pico-1943's
 * c1943_input.c.
 */
#include "sal_input.h"

#include "sal.h"

/* A coin switch is a pulse: held this many frames. */
#define COIN_FRAMES 6

void sal_pads_reset(sal_pads_t *s)
{
    s->prev = s->prev2 = ~0u;
    s->select_used = s->select2_used = 1;
    s->coin = s->coin2 = 0;
    s->wait_release = 1;
}

static uint32_t player(uint32_t pad, int rapid_a, int rapid_phase)
{
    uint32_t in = 0;
    if (pad & SAL_PAD_RIGHT)
        in |= SAL_IN_RIGHT;
    if (pad & SAL_PAD_LEFT)
        in |= SAL_IN_LEFT;
    if (pad & SAL_PAD_DOWN)
        in |= SAL_IN_DOWN;
    if (pad & SAL_PAD_UP)
        in |= SAL_IN_UP;
    if ((pad & SAL_PAD_A) && (!rapid_a || rapid_phase))
        in |= SAL_IN_B2;
    if (pad & SAL_PAD_B)
        in |= SAL_IN_B1;
    if (pad & (SAL_PAD_X | SAL_PAD_Y))
        in |= SAL_IN_B3;
    return in;
}

uint32_t sal_pads_map(sal_pads_t *s, uint32_t p1, uint32_t p2, int rapid_a, int *settings, int *fps)
{
    const uint32_t combo = SAL_PAD_SELECT | SAL_PAD_START;
    const uint32_t pushed = p1 & ~s->prev;
    s->frame++;
    *settings = (p1 & combo) == combo && (s->prev & combo) != combo;
    *fps = (p1 & SAL_PAD_START) && (pushed & SAL_PAD_A);

    if (p1 & SAL_PAD_SELECT)
    {
        if (p1 & ~(uint32_t)SAL_PAD_SELECT)
            s->select_used = 1;
    }
    else if (s->prev & SAL_PAD_SELECT)
    {
        if (!s->select_used)
            s->coin = COIN_FRAMES;
        s->select_used = 0;
    }
    if (pushed & SAL_PAD_COIN)
        s->coin = COIN_FRAMES;
    /* the second controller's SELECT: a coin in the second slot */
    if (p2 & SAL_PAD_SELECT)
    {
        if (p2 & ~(uint32_t)SAL_PAD_SELECT)
            s->select2_used = 1;
    }
    else if (s->prev2 & SAL_PAD_SELECT)
    {
        if (!s->select2_used)
            s->coin2 = COIN_FRAMES;
        s->select2_used = 0;
    }
    s->prev = p1;
    s->prev2 = p2;

    if (s->wait_release)
    {
        s->wait_release = (p1 | p2) != 0;
        s->coin = s->coin2 = 0;
        return 0;
    }

    uint32_t in = 0;
    if (s->coin > 0)
    {
        s->coin--;
        in |= SAL_IN_COIN1;
    }
    if (s->coin2 > 0)
    {
        s->coin2--;
        in |= SAL_IN_COIN2;
    }
    if (p1 & SAL_PAD_SELECT)
        return in; /* a modifier combination, not game input */
    if (p2 & SAL_PAD_SELECT)
        p2 = 0; /* the same on the second controller */

    const int rapid_phase = (s->frame & 4) != 0;
    if ((p1 & SAL_PAD_START) && !(p1 & SAL_PAD_A))
        in |= SAL_IN_START1;
    if ((p2 & SAL_PAD_START) || (p1 & SAL_PAD_START2))
        in |= SAL_IN_START2;
    /* while START is held, A toggles the frame rate rather than shoots */
    in |= player((p1 & SAL_PAD_START) ? (p1 & ~(uint32_t)SAL_PAD_A) : p1, rapid_a, rapid_phase);
    in |= SAL_IN_P2(player(p2, rapid_a, rapid_phase));
    return in;
}
