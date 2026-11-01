/*
 * Musashi's configuration for the Salamander board: a 68000 only, with the
 * memory reached through the inline functions of sal_bus.h.
 *
 * Interrupts are acknowledged through sal_m68k_int_ack() (the autovector;
 * the line is the game's to clear). Prefetch, address errors, tracing and the hooks are
 * off; the 64-bit multiply and divide helpers are on. The upstream options are
 * documented in Musashi's own m68kconf.h.
 */
#ifndef M68KCONF__HEADER
#define M68KCONF__HEADER

#define M68K_OPT_OFF 0
#define M68K_OPT_ON 1
#define M68K_OPT_SPECIFY_HANDLER 2

#define M68K_COMPILE_FOR_MAME M68K_OPT_OFF

#define M68K_EMULATE_010 M68K_OPT_OFF
#define M68K_EMULATE_EC020 M68K_OPT_OFF
#define M68K_EMULATE_020 M68K_OPT_OFF
#define M68K_EMULATE_030 M68K_OPT_OFF
#define M68K_EMULATE_040 M68K_OPT_OFF

/* immediate and PC-relative reads come from the ROM pages (sal_bus.h) */
#define M68K_SEPARATE_READS M68K_OPT_ON
#define M68K_SIMULATE_PD_WRITES M68K_OPT_OFF

#define M68K_EMULATE_INT_ACK M68K_OPT_SPECIFY_HANDLER
#define M68K_INT_ACK_CALLBACK(A) sal_m68k_int_ack(A)

#define M68K_EMULATE_BKPT_ACK M68K_OPT_OFF
#define M68K_BKPT_ACK_CALLBACK() ((void)0)
#define M68K_EMULATE_TRACE M68K_OPT_OFF
#define M68K_EMULATE_RESET M68K_OPT_OFF
#define M68K_RESET_CALLBACK() ((void)0)
#define M68K_CMPILD_HAS_CALLBACK M68K_OPT_OFF
#define M68K_CMPILD_CALLBACK(v, r) ((void)0)
#define M68K_RTE_HAS_CALLBACK M68K_OPT_OFF
#define M68K_RTE_CALLBACK() ((void)0)
#define M68K_TAS_HAS_CALLBACK M68K_OPT_OFF
#define M68K_TAS_CALLBACK() 1
#define M68K_ILLG_HAS_CALLBACK M68K_OPT_OFF
#define M68K_ILLG_CALLBACK(opcode) 0
#define M68K_TRAP_HAS_CALLBACK M68K_OPT_OFF
#define M68K_TRAP_CALLBACK(trap) 0
#define M68K_EMULATE_FC M68K_OPT_OFF
#define M68K_SET_FC_CALLBACK(A) ((void)0)
#define M68K_MONITOR_PC M68K_OPT_OFF
#define M68K_SET_PC_CALLBACK(A) ((void)0)

/* The host harness counts instructions per ROM page and per handler
 * (--profile) through the instruction hook. */
#ifdef SAL_M68K_PROFILE
#define M68K_INSTRUCTION_HOOK M68K_OPT_SPECIFY_HANDLER
#define M68K_INSTRUCTION_CALLBACK(pc) sal_m68k_profile_hook(pc)
void sal_m68k_profile_hook(unsigned int pc);
#else
#define M68K_INSTRUCTION_HOOK M68K_OPT_OFF
#define M68K_INSTRUCTION_CALLBACK(pc) ((void)0)
#endif

#define M68K_EMULATE_PREFETCH M68K_OPT_OFF
#define M68K_EMULATE_ADDRESS_ERROR M68K_OPT_OFF
#define M68K_LOG_ENABLE M68K_OPT_OFF
#define M68K_LOG_1010_1111 M68K_OPT_OFF
#define M68K_LOG_TRAP M68K_OPT_OFF
#define M68K_LOG_FILEHANDLE stderr
#define M68K_EMULATE_PMMU M68K_OPT_OFF
#define M68K_USE_64_BIT M68K_OPT_ON

/* The memory: the inline functions of sal_bus.h in place of the functions
 * Musashi's m68k.h would declare (which our copy no longer does). */
#include "sal_bus.h"
#define m68k_read_memory_8(A) sal_m68k_read8(A)
#define m68k_read_memory_16(A) sal_m68k_read16(A)
#define m68k_read_memory_32(A) sal_m68k_read32(A)
#define m68k_read_immediate_16(A) sal_m68k_fetch16(A)
#define m68k_read_immediate_32(A) sal_m68k_fetch32(A)
#define m68k_read_pcrelative_8(A) sal_m68k_read8(A)
#define m68k_read_pcrelative_16(A) sal_m68k_fetch16(A)
#define m68k_read_pcrelative_32(A) sal_m68k_fetch32(A)
#define m68k_write_memory_8(A, V) sal_m68k_write8(A, V)
#define m68k_write_memory_16(A, V) sal_m68k_write16(A, V)
#define m68k_write_memory_32(A, V) sal_m68k_write32(A, V)

/* The dispatch tables (tools/m68k_dispatch.py) in place of Musashi's jump
 * table; and the handlers to keep in SRAM on the board. */
#define SAL_M68K_DISPATCH 1
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define SAL_M68K_HOT(f) __not_in_flash_func(f)
#else
#define SAL_M68K_HOT(f) f
#endif

#endif /* M68KCONF__HEADER */
