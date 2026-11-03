/*
 * Build glue shared by the Salamander core. The core is plain C with no Pico
 * dependencies, so the same sources build for the board and for the host
 * harness in hosttest/. On the board the hot paths go to SRAM.
 *
 * So do the tables they read: SAL_RAM_DATA, the SDK's __not_in_flash (not
 * merely leaving out const: GCC puts a table that is never written in flash
 * all the same). Flash is read through the XIP cache, which the main CPU's
 * ROM (in PSRAM or flash, behind the same cache) and core1's sound code compete for;
 * a table there misses all the time, and each miss waits for the QSPI bus.
 * The same goes for library calls (memset, memcpy, 64-bit division), which
 * live in flash: the hot paths use loops.
 */
#ifndef SAL_PORT_H
#define SAL_PORT_H

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define SAL_HOT(f) __not_in_flash_func(f)
#define SAL_RAM_DATA __not_in_flash("sal")
/* for const tables: a section of their own, as a read-only and a writable
 * object cannot share one */
#define SAL_RAM_CONST __not_in_flash("salro")
#else
#define SAL_HOT(f) f
#define SAL_RAM_DATA
#define SAL_RAM_CONST
#endif

/* The sound CPU's core (z80.c, 80 KB of code) runs from flash unless the
 * build asks for SRAM: core1 runs it between the display's interrupts and
 * has the time, and the SRAM is wanted elsewhere. */
#if SAL_Z80_IN_SRAM
#define SAL_Z80_HOT(f) SAL_HOT(f)
#else
#define SAL_Z80_HOT(f) f
#endif

#endif
