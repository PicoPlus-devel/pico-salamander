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
 * The DIP switches start at MAME's defaults: 1 coin 1 credit, 3 lives, normal
 * difficulty, demo sounds on, upright cabinet, flip screen off. A menu offers
 * those of dips_salamand and dips_lifefrcej. Not offered are flip screen and
 * cabinet, as the flipped picture is not emulated, and the coinage that
 * disables the coin slots, as no game could then be started.
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

#define DIP_HELP(text) text ". Saving restarts game"
#define DIP(label, text, port, mask, names, values) {label, DIP_HELP(text), port, mask, N(values), names, values}

static const char *const coin_names[] = {
    "4 coins, 1 credit",  "3 coins, 1 credit",  "2 coins, 1 credit",  "3 coins, 2 credits", "4 coins, 3 credits",
    "1 coin, 1 credit",   "3 coins, 4 credits", "2 coins, 3 credits", "1 coin, 2 credits",  "2 coins, 5 credits",
    "1 coin, 3 credits",  "1 coin, 4 credits",  "1 coin, 5 credits",  "1 coin, 6 credits",  "1 coin, 7 credits",
};
static const uint8_t coin_a[] = {0x02, 0x05, 0x08, 0x04, 0x01, 0x0f, 0x03, 0x07,
                                 0x0e, 0x06, 0x0d, 0x0c, 0x0b, 0x0a, 0x09};
static const uint8_t coin_b[] = {0x20, 0x50, 0x80, 0x40, 0x10, 0xf0, 0x30, 0x70,
                                 0xe0, 0x60, 0xd0, 0xc0, 0xb0, 0xa0, 0x90};

static const char *const off_on_names[] = {"OFF", "ON"};
static const uint8_t off_on_values[] = {0x80, 0x00}; /* b7 of its port, active low */

static const char *const slot_names[] = {"1", "2"};
static const uint8_t slot_values[] = {0x04, 0x00};

static const char *const difficulty_names[] = {"Easy", "Normal", "Hard", "Hardest"};
static const uint8_t difficulty_values[] = {0x60, 0x40, 0x20, 0x00};

static const uint8_t lives_values[] = {0x03, 0x02, 0x01, 0x00};

static const char *const salamand_lives_names[] = {"2", "3", "5", "7"};
static const char *const credit_names[] = {"1", "3", "5", "9"};
static const uint8_t credit_values[] = {0x18, 0x10, 0x08, 0x00};

static const sal_dip_t dips_salamand[] = {
    DIP("Coinage", "Coins per credit", SAL_DIP_DSW0, 0x0f, coin_names, coin_a),
    DIP("Lives", "Lives at start", SAL_DIP_DSW1, 0x03, salamand_lives_names, lives_values),
    DIP("Coin slots", "Coin slots in use", SAL_DIP_DSW1, 0x04, slot_names, slot_values),
    DIP("Max credits", "Credit limit", SAL_DIP_DSW1, 0x18, credit_names, credit_values),
    DIP("Difficulty", "Game difficulty", SAL_DIP_DSW1, 0x60, difficulty_names, difficulty_values),
    DIP("Demo sounds", "Sound in attract", SAL_DIP_DSW1, 0x80, off_on_names, off_on_values),
    DIP("Service mode", "Test screens", SAL_DIP_IN0, 0x80, off_on_names, off_on_values),
};

static const char *const lifefrcej_lives_names[] = {"3", "4", "5", "6"};
static const char *const bonus_names[] = {"70k, every 200k", "100k, every 300k", "70k only", "100k only"};
static const uint8_t bonus_values[] = {0x18, 0x10, 0x08, 0x00};

static const sal_dip_t dips_lifefrcej[] = {
    DIP("Coin A", "Coin slot 1", SAL_DIP_DSW0, 0x0f, coin_names, coin_a),
    DIP("Coin B", "Coin slot 2", SAL_DIP_DSW0, 0xf0, coin_names, coin_b),
    DIP("Lives", "Lives at start", SAL_DIP_DSW1, 0x03, lifefrcej_lives_names, lives_values),
    DIP("Coin slots", "Coin slots in use", SAL_DIP_DSW1, 0x04, slot_names, slot_values),
    DIP("Bonus life", "Extra lives", SAL_DIP_DSW1, 0x18, bonus_names, bonus_values),
    DIP("Difficulty", "Game difficulty", SAL_DIP_DSW1, 0x60, difficulty_names, difficulty_values),
    DIP("Demo sounds", "Sound in attract", SAL_DIP_DSW1, 0x80, off_on_names, off_on_values),
    DIP("Service mode", "Test screens", SAL_DIP_IN0, 0x80, off_on_names, off_on_values),
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
            .dips = {dips_salamand, dips_salamand, dips_salamand},
            .ndips = {N(dips_salamand), N(dips_salamand), N(dips_salamand)},
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
            .dips = {dips_salamand, dips_lifefrcej},
            .ndips = {N(dips_salamand), N(dips_lifefrcej)},
        },
};

void sal_dip_defaults(sal_set_id_t set, int variant, uint8_t *ports)
{
    ports[SAL_DIP_DSW0] = sal_sets[set].dsw0[variant];
    ports[SAL_DIP_DSW1] = sal_sets[set].dsw1[variant];
    ports[SAL_DIP_IN0] = 0xe0; /* flip screen and service off, b6 unused: active low */
}

int sal_dip_choice(const sal_dip_t *d, const uint8_t *ports)
{
    for (int c = 0; c < d->count; c++)
        if ((ports[d->port] & d->mask) == d->values[c])
            return c;
    return -1;
}

void sal_dip_select(const sal_dip_t *d, uint8_t *ports, int choice)
{
    ports[d->port] = (uint8_t)((ports[d->port] & ~d->mask) | d->values[choice]);
}

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
