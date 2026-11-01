/*
 * The ROM sets, from MAME's konami/nemesis.cpp. The files are found by size
 * and CRC32 (romload.c), so their names are only for messages.
 *
 * Salamander: MAME's salamand (version D) with its clones salamandj (version
 * J) and salamandt (the Tecfri licence, the same program in smaller ROMs). The
 * versions differ in the two program ROMs of the main CPU; the mask ROMs with
 * the data, the sound ROM and the speech ROM are shared (salamandt has a
 * sound ROM of its own). A file that two versions share is listed once, for
 * both: the loader matches by CRC and would otherwise find the same file
 * twice. MAME's merged salamand.zip holds every version (the clones in
 * subfolders); a split or non-merged set holds one. The versions are tried
 * in this order: salamand, salamandj, salamandt.
 *
 * Lifeforce: MAME's lifefrce (US) and lifefrcej (Japan), clones of salamand
 * in MAME, so a split lifefrce.zip needs salamand.zip beside it for the three
 * mask ROMs it shares with the parent. Lifeforce (Japan) is the version with
 * three buttons. The US version is tried first.
 *
 * The DIP switches are MAME's defaults: 1 coin 1 credit, 3 lives, normal
 * difficulty, demo sounds on, upright cabinet, flip screen off.
 */
#include "sal.h"

#define N(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))

const uint32_t sal_region_size[SAL_R_COUNT] = {
    [SAL_R_MAIN] = 0x80000,
    [SAL_R_SOUND] = 0x8000,
    [SAL_R_VLM] = 0x4000,
    [SAL_R_PCM] = 0x20000,
};

const uint32_t sal_part_size[SAL_P_COUNT] = {
    [SAL_P_MAIN] = 0x80000,
    [SAL_P_SOUND] = 0x8000,
    [SAL_P_VLM] = 0x4000,
    [SAL_P_PCM] = 0x20000,
};

#define V_D 0x01 /* salamand */
#define V_J 0x02 /* salamandj */
#define V_T 0x04 /* salamandt */

static const sal_romfile_t files_salamand[] = {
    /* the main CPU's program, per version (salamandt's 5 and 6 are these) */
    {"587-d02.18b", 0xa42297f9, 0x10000, SAL_R_MAIN, 2, 0x00000, V_D | V_T},
    {"587-d05.18c", 0xf9130b0a, 0x10000, SAL_R_MAIN, 2, 0x00001, V_D | V_T},
    {"587-j02.18b", 0xf68ee99a, 0x10000, SAL_R_MAIN, 2, 0x00000, V_J},
    {"587-j05.18c", 0x72c16128, 0x10000, SAL_R_MAIN, 2, 0x00001, V_J},
    /* the data, in mask ROMs (D and J) or EPROMs (T) */
    {"587-c03.17b", 0xe5caf6e6, 0x20000, SAL_R_MAIN, 2, 0x40000, V_D | V_J},
    {"587-c06.17c", 0xc2f567ea, 0x20000, SAL_R_MAIN, 2, 0x40001, V_D | V_J},
    {"10_27512.17b", 0xb83e8724, 0x10000, SAL_R_MAIN, 2, 0x40000, V_T},
    {"4_27512.17c", 0xa6ef6dc4, 0x10000, SAL_R_MAIN, 2, 0x40001, V_T},
    {"9_27512.17b", 0xb4d2fec9, 0x10000, SAL_R_MAIN, 2, 0x60000, V_T},
    {"3_27512.17c", 0x6ea59643, 0x10000, SAL_R_MAIN, 2, 0x60001, V_T},
    /* the sound CPU */
    {"587-d09.11j", 0x5020972c, 0x8000, SAL_R_SOUND, 1, 0x0000, V_D | V_J},
    {"2_tmm24256ap.11j", 0x7eb8bb88, 0x8000, SAL_R_SOUND, 1, 0x0000, V_T},
    /* the speech (salamandt's 1_27128.8g is the same) */
    {"587-d08.8g", 0xf9ac6b82, 0x4000, SAL_R_VLM, 1, 0x0000, 0},
    /* the samples */
    {"587-c01.10a", 0x09fe0632, 0x20000, SAL_R_PCM, 1, 0x00000, V_D | V_J},
    {"8_27512.10a", 0xcf477da4, 0x10000, SAL_R_PCM, 1, 0x00000, V_T},
    {"7_27512.10a", 0x52384e79, 0x10000, SAL_R_PCM, 1, 0x10000, V_T},
};

#define V_US 0x01 /* lifefrce */
#define V_JP 0x02 /* lifefrcej */

static const sal_romfile_t files_lifefrce[] = {
    /* the main CPU's program, per version */
    {"587-l02.18b", 0x4a44da18, 0x10000, SAL_R_MAIN, 2, 0x00000, V_US},
    {"587-l05.18c", 0x2f8c1cbd, 0x10000, SAL_R_MAIN, 2, 0x00001, V_US},
    {"587-n02.18b", 0x235dba71, 0x10000, SAL_R_MAIN, 2, 0x00000, V_JP},
    {"587-n05.18c", 0x054e569f, 0x10000, SAL_R_MAIN, 2, 0x00001, V_JP},
    /* the data: the US version has Salamander's mask ROMs (6107, 6108) */
    {"6107.17b", 0xe5caf6e6, 0x20000, SAL_R_MAIN, 2, 0x40000, V_US},
    {"6108.17c", 0xc2f567ea, 0x20000, SAL_R_MAIN, 2, 0x40001, V_US},
    {"587-n03.17b", 0x9041f850, 0x20000, SAL_R_MAIN, 2, 0x40000, V_JP},
    {"587-n06.17c", 0xfba8b6aa, 0x20000, SAL_R_MAIN, 2, 0x40001, V_JP},
    /* the sound CPU */
    {"587-k09.11j", 0x2255fe8c, 0x8000, SAL_R_SOUND, 1, 0x0000, V_US},
    {"587-n09.11j", 0xe8496150, 0x8000, SAL_R_SOUND, 1, 0x0000, V_JP},
    /* the speech (587-n08.8g is the same) and the samples (6106.10a, too) */
    {"587-k08.8g", 0x7f0e9b41, 0x4000, SAL_R_VLM, 1, 0x0000, 0},
    {"587-c01.10a", 0x09fe0632, 0x20000, SAL_R_PCM, 1, 0x00000, 0},
};

const sal_set_info_t sal_sets[SAL_SET_COUNT] = {
    [SAL_SET_SALAMAND] =
        {
            .title = "Salamander",
            .folder = "SALAMAND",
            .mameset = "salamand",
            .variant_set = {"salamand", "salamandj", "salamandt"},
            .variant_name = {"version D", "version J", "Tecfri licence"},
            .nvariants = 3,
            .files = files_salamand,
            .nfiles = N(files_salamand),
            .dsw0 = {0xff, 0xff, 0xff},
            .dsw1 = {0x42, 0x42, 0x42},
        },
    [SAL_SET_LIFEFRCE] =
        {
            .title = "Lifeforce",
            .folder = "LIFEFRCE",
            .mameset = "lifefrce",
            .variant_set = {"lifefrce", "lifefrcej"},
            .variant_name = {"US", "Japan"},
            .nvariants = 2,
            .files = files_lifefrce,
            .nfiles = N(files_lifefrce),
            .dsw0 = {0xff, 0xff},
            .dsw1 = {0x42, 0x5b},
        },
};

sal_files_t sal_variant_files(sal_set_id_t set, int v)
{
    const sal_set_info_t *s = &sal_sets[set];
    sal_files_t m = 0;
    for (int i = 0; i < s->nfiles; i++)
        if (!s->files[i].variants || (s->files[i].variants & (1u << v)))
            m |= (sal_files_t)1 << i;
    return m;
}

int sal_variant_found(sal_set_id_t set, sal_files_t found)
{
    for (int v = 0; v < sal_sets[set].nvariants; v++)
    {
        const sal_files_t need = sal_variant_files(set, v);
        if ((found & need) == need)
            return v;
    }
    return -1;
}

int sal_variant_nearest(sal_set_id_t set, sal_files_t found)
{
    int best = 0, best_missing = 1000;
    for (int v = 0; v < sal_sets[set].nvariants; v++)
    {
        const sal_files_t missing = sal_variant_files(set, v) & ~found;
        const int n = __builtin_popcountll(missing);
        if (n < best_missing)
        {
            best_missing = n;
            best = v;
        }
    }
    return best;
}
