/*
 * The controllers onto the cabinet, shared by the board (main.cpp) and the
 * host harness, so that both behave the same:
 *
 *   d-pad        joystick (8-way)
 *   A            shoot (Salamander: shoot and missile)
 *   B            power-up
 *   X or Y       missile (Lifeforce Japan, which has a third button)
 *   SELECT       coin, when released, unless another button was pressed
 *                while it was held (SELECT + START opens the settings menu)
 *   START        1 player start; START on the second controller: 2 players
 *   START + A    shows or hides the frame rate
 *
 * The second controller plays player 2. Rapid fire, when set for A: 4 frames
 * pressed, 4 released.
 */
#ifndef SAL_INPUT_H
#define SAL_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pad buttons: those of io::GamePadState mapped by the board, the host's
 * keyboard (host_sdl.h uses the same bits). */
#define SAL_PAD_A 0x001
#define SAL_PAD_B 0x002
#define SAL_PAD_X 0x004
#define SAL_PAD_Y 0x008
#define SAL_PAD_SELECT 0x010
#define SAL_PAD_START 0x020
#define SAL_PAD_UP 0x040
#define SAL_PAD_DOWN 0x080
#define SAL_PAD_LEFT 0x100
#define SAL_PAD_RIGHT 0x200
#define SAL_PAD_COIN 0x400   /* a coin key of its own (host: 5) */
#define SAL_PAD_START2 0x800 /* a 2 player start key (host: 2) */

typedef struct
{
    uint32_t prev, prev2;
    int select_used;
    int coin;
    int wait_release;
    uint32_t frame;
} sal_pads_t;

/* Clears the state; buttons held now count only once released (after the
 * settings menu, which returns with buttons still down). */
void sal_pads_reset(sal_pads_t *s);

/* One frame: the two controllers to SAL_IN_* bits. *settings is set when
 * SELECT + START was pressed, *fps when START + A was. */
uint32_t sal_pads_map(sal_pads_t *s, uint32_t p1, uint32_t p2, int rapid_a, int *settings, int *fps);

#ifdef __cplusplus
}
#endif

#endif
