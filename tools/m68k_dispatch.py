#!/usr/bin/env python3
"""
m68k_dispatch.py - the 68000's opcode dispatch tables for the board, from
Musashi's generated m68kops.c.

    tools/m68k_dispatch.py <m68kops.c from m68kmake> <out dir> [--hot FILE]

Musashi builds, at run time, a jump table of 65536 function pointers and
65536 cycle counts per CPU type: 320 KB of RAM. The board cannot spare it,
so this script does the same work ahead of time and writes

    <out dir>/m68kops.c             the opcode handlers of Musashi's file, the
                                    table builder left out, and the dispatch
                                    tables included at the end
    <out dir>/sal_m68k_dispatch.h   sal_m68k_index[65536]: for each opcode the
                                    number of its (handler, cycles) pair, const
                                    (flash); sal_m68k_fn[] and sal_m68k_cyc[]:
                                    the pairs (RAM: a few KB)
    <out dir>/sal_m68k_names.h      the handlers' names, for the profile

The order the handlers claim opcodes in is Musashi's: the handler table in
the order m68kmake wrote it, later entries overriding earlier ones, which is
what m68ki_build_opcode_table does phase by phase. The phases are checked.

--hot FILE names handlers (one per line, as in a profile from the host
harness) to be placed in SRAM on the board: their definitions are marked
with SAL_M68K_HOT.
"""

import re
import sys

PHASES = [None, 0xFF00, 0xF1F8, 0xFFF0, 0xF1FF, 0xFFF8, 0xFFFF]


def main():
    args = sys.argv[1:]
    hot = set()
    if "--hot" in args:
        i = args.index("--hot")
        with open(args[i + 1]) as f:
            hot = {l.strip() for l in f if l.strip() and not l.startswith("#")}
        del args[i : i + 2]
    if len(args) != 2:
        print(__doc__)
        return 1
    src = open(args[0]).read()
    outdir = args[1]

    # the handler table
    tstart = src.index("static const opcode_handler_struct m68k_opcode_handler_table[] =")
    tend = src.index("};", tstart)
    entries = []
    for m in re.finditer(r"\{\s*(m68k_op_\w+)\s*,\s*(0x[0-9a-f]+)\s*,\s*(0x[0-9a-f]+)\s*,\s*\{([^}]*)\}\s*\}", src[tstart:tend]):
        name, mask, match, cyc = m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4)
        cycles = [int(c) for c in cyc.split(",")]
        entries.append((name, mask, match, cycles[0]))  # the 68000's cycles
    if not entries:
        print("no handler table found")
        return 1

    # check the phase order m68ki_build_opcode_table consumes the table in
    phase = 0
    for name, mask, match, c in entries:
        while phase < len(PHASES) and not (PHASES[phase] is None and mask not in PHASES[1:] or mask == PHASES[phase]):
            phase += 1
        if phase >= len(PHASES):
            print(f"handler {name}: mask {mask:04x} out of the builder's phase order")
            return 1

    # the jump table: in order, later entries override
    pair_of = {}
    pairs = []  # (name, cycles)

    def pair_id(name, cycles):
        key = (name, cycles)
        if key not in pair_of:
            pair_of[key] = len(pairs)
            pairs.append(key)
        return pair_of[key]

    illegal = pair_id("m68k_op_illegal", 0)
    index = [illegal] * 0x10000
    for name, mask, match, cycles in entries:
        pid = pair_id(name, cycles)
        free = (~mask) & 0xFFFF
        # every subset of the free bits
        sub = free
        while True:
            index[match | sub] = pid
            if sub == 0:
                break
            sub = (sub - 1) & free
    if len(pairs) > 0xFFFF:
        print("too many handlers")
        return 1

    # the handlers, the table builder left out
    builder = src.index("/* ========================= OPCODE TABLE BUILDER ========================= */")
    builder = src.rindex("/* ====", 0, builder)
    ops = src[:builder]
    ops = ops.replace("CYC_INSTRUCTION[REG_IR]", "SAL_M68K_CYC_IR")  # m68kcpu.h's name for it
    ops = ops.replace('#include "m68kcpu.h"\n', '#include "m68kcpu.h"\n#include "sal_m68k_dispatch.h"\n', 1)
    nhot = 0
    if hot:
        def mark(m):
            nonlocal nhot
            if m.group(1) in hot:
                nhot += 1
                return f"static void SAL_M68K_HOT({m.group(1)})(void)"
            return m.group(0)
        ops = re.sub(r"static void (m68k_op_\w+)\(void\)", mark, ops)
    with open(f"{outdir}/m68kops.c", "w") as f:
        f.write(ops)
        f.write("\n/* The dispatch tables: tools/m68k_dispatch.py, see sal_m68k_dispatch.h. */\n")
        f.write(f"void (*sal_m68k_fn[{len(pairs)}])(void) = {{\n")
        for name, c in pairs:
            f.write(f"    {name},\n")
        f.write("};\n")
        f.write(f"uint8_t sal_m68k_cyc[{len(pairs)}] = {{\n")
        for i in range(0, len(pairs), 24):
            f.write("    " + ", ".join(str(c) for _, c in pairs[i : i + 24]) + ",\n")
        f.write("};\n")
        f.write("const uint16_t sal_m68k_index[0x10000] = {\n")
        for i in range(0, 0x10000, 16):
            f.write("    " + ", ".join(str(v) for v in index[i : i + 16]) + ",\n")
        f.write("};\n")

    # the handlers' names, for the host harness's profile
    with open(f"{outdir}/sal_m68k_names.h", "w") as f:
        f.write("/* Generated by tools/m68k_dispatch.py - do not edit. The handlers' names,\n"
                " * indexed as sal_m68k_fn, for the host harness's profile. */\n")
        f.write(f"static const char *const sal_m68k_names[{len(pairs)}] = {{\n")
        for name, c in pairs:
            f.write(f'    "{name}",\n')
        f.write("};\n")

    with open(f"{outdir}/sal_m68k_dispatch.h", "w") as f:
        f.write(
            "/* Generated by tools/m68k_dispatch.py from Musashi's m68kops.c - do not edit.\n"
            " * The opcode dispatch tables (defined at the end of m68kops.c): an opcode's\n"
            " * entry in sal_m68k_index is the number of its handler and cycle count in\n"
            " * sal_m68k_fn and sal_m68k_cyc. */\n"
            "#ifndef SAL_M68K_DISPATCH_H\n#define SAL_M68K_DISPATCH_H\n\n#include <stdint.h>\n\n"
            f"#define SAL_M68K_NHANDLERS {len(pairs)}\n"
            f"#define SAL_M68K_NHOT {nhot}\n"
            "extern void (*sal_m68k_fn[SAL_M68K_NHANDLERS])(void);\n"
            "extern uint8_t sal_m68k_cyc[SAL_M68K_NHANDLERS];\n"
            "extern const uint16_t sal_m68k_index[0x10000];\n\n"
            "#endif\n"
        )
    print(f"{len(entries)} handler entries, {len(pairs)} (handler, cycles) pairs, {nhot} in SRAM")
    return 0


if __name__ == "__main__":
    sys.exit(main())
