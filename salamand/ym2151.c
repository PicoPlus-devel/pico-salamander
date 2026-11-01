/*
 * Yamaha YM2151 (OPM); see ym2151.h. MAME's ym2151.cpp (0.229) in C, the
 * samples made at the output rate: every per-sample step of the chip is
 * scaled by the ratio of the chip's rate to the output rate (freq_scale),
 * as the pre-2015 MAME core did with its `freqbase`. The frequency table
 * (11 octaves of 768 steps, 33 KB in MAME) is computed from the chip's ROM
 * of phase increments when it is needed instead of kept in memory.
 */
#include "ym2151.h"

#include "sal_port.h"

#include <string.h>

#define FREQ_SH 16  /* 16.16 fixed point (frequency calculations) */
#define EG_SH 16    /* 16.16 fixed point (envelope generator timing) */
#define LFO_SH 10   /* 22.10 fixed point (LFO calculations) */
#define FREQ_MASK ((1 << FREQ_SH) - 1)

#define ENV_BITS 10
#define ENV_LEN (1 << ENV_BITS)
#define ENV_STEP (128.0 / ENV_LEN)
#define MAX_ATT_INDEX (ENV_LEN - 1) /* 1023 */
#define MIN_ATT_INDEX (0)

#define EG_ATT 4
#define EG_DEC 3
#define EG_SUS 2
#define EG_REL 1
#define EG_OFF 0

#define ENV_QUIET (YM_TL_TAB_LEN >> 3)
#define RATE_STEPS 8

/* The tables read for every sample are in SRAM on the board. */
static int16_t SAL_RAM_DATA tl_tab[YM_TL_TAB_LEN];
static uint16_t SAL_RAM_DATA sin_tab[YM_SIN_LEN];
static uint32_t d1l_tab[16];

static const uint8_t SAL_RAM_CONST eg_inc[19 * RATE_STEPS] = {
    /*cycle:0 1  2 3  4 5  6 7*/
    /* 0 */ 0, 1, 0, 1, 0, 1, 0, 1, /* rates 00..11 0 (increment by 0 or 1) */
    /* 1 */ 0, 1, 0, 1, 1, 1, 0, 1, /* rates 00..11 1 */
    /* 2 */ 0, 1, 1, 1, 0, 1, 1, 1, /* rates 00..11 2 */
    /* 3 */ 0, 1, 1, 1, 1, 1, 1, 1, /* rates 00..11 3 */
    /* 4 */ 1, 1, 1, 1, 1, 1, 1, 1, /* rate 12 0 (increment by 1) */
    /* 5 */ 1, 1, 1, 2, 1, 1, 1, 2, /* rate 12 1 */
    /* 6 */ 1, 2, 1, 2, 1, 2, 1, 2, /* rate 12 2 */
    /* 7 */ 1, 2, 2, 2, 1, 2, 2, 2, /* rate 12 3 */
    /* 8 */ 2, 2, 2, 2, 2, 2, 2, 2, /* rate 13 0 (increment by 2) */
    /* 9 */ 2, 2, 2, 4, 2, 2, 2, 4, /* rate 13 1 */
    /*10 */ 2, 4, 2, 4, 2, 4, 2, 4, /* rate 13 2 */
    /*11 */ 2, 4, 4, 4, 2, 4, 4, 4, /* rate 13 3 */
    /*12 */ 4, 4, 4, 4, 4, 4, 4, 4, /* rate 14 0 (increment by 4) */
    /*13 */ 4, 4, 4, 8, 4, 4, 4, 8, /* rate 14 1 */
    /*14 */ 4, 8, 4, 8, 4, 8, 4, 8, /* rate 14 2 */
    /*15 */ 4, 8, 8, 8, 4, 8, 8, 8, /* rate 14 3 */
    /*16 */ 8, 8, 8, 8, 8, 8, 8, 8, /* rates 15 0, 15 1, 15 2, 15 3 (increment by 8) */
    /*17 */ 16, 16, 16, 16, 16, 16, 16, 16, /* rates 15 2, 15 3 for attack */
    /*18 */ 0, 0, 0, 0, 0, 0, 0, 0, /* infinity rates for attack and decay(s) */
};

#define O(a) (a * RATE_STEPS)
/* Envelope Generator rates (32 + 64 rates + 32 RKS); no O(17): in the code */
static const uint8_t SAL_RAM_CONST eg_rate_select[32 + 64 + 32] = {
    /* 32 dummy (infinite time) rates */
    O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18),
    O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18), O(18),
    /* rates 00-11 */
    O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3),
    O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3),
    O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3), O(0), O(1), O(2), O(3),
    /* rate 12 */
    O(4), O(5), O(6), O(7),
    /* rate 13 */
    O(8), O(9), O(10), O(11),
    /* rate 14 */
    O(12), O(13), O(14), O(15),
    /* rate 15 */
    O(16), O(16), O(16), O(16),
    /* 32 dummy rates (same as 15 3) */
    O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16),
    O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16), O(16)};
#undef O

/* Envelope Generator counter shifts (32 + 64 rates + 32 RKS) */
static const uint8_t SAL_RAM_CONST eg_rate_shift[32 + 64 + 32] = {
    /* 32 infinite time rates */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* rates 00-11 */
    11, 11, 11, 11, 10, 10, 10, 10, 9, 9, 9, 9, 8, 8, 8, 8, 7, 7, 7, 7, 6, 6, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4,
    3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0,
    /* rate 12 */
    0, 0, 0, 0,
    /* rate 13 */
    0, 0, 0, 0,
    /* rate 14 */
    0, 0, 0, 0,
    /* rate 15 */
    0, 0, 0, 0,
    /* 32 dummy rates (same as 15 3) */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* DT2 defines offset in cents from base note: an offset in the frequency
 * table. User's Manual page 22, values / 1.5625. DT2=0 DT2=1 DT2=2 DT2=3: 0
 * 600 781 950. */
static const uint32_t dt2_tab[4] = {0, 384, 500, 608};

/* DT1 defines offset in Hertz from base note. The detune table in the
 * YM2151 User's Manual is wrong (verified on the real chip). */
static const uint8_t dt1_tab[4 * 32] = {
    /* DT1=0 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* DT1=1 */
    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8,
    /* DT1=2 */
    1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 16, 16, 16,
    /* DT1=3 */
    2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 20, 22, 22, 22, 22};

/* The chip's ROM of phase increments: 768 steps of a 12-note octave
 * (10.10 fixed point). */
static const uint16_t SAL_RAM_CONST phaseinc_rom[768] = {
    1299, 1300, 1301, 1302, 1303, 1304, 1305, 1306, 1308, 1309, 1310, 1311, 1313, 1314, 1315, 1316, 1318, 1319, 1320,
    1321, 1322, 1323, 1324, 1325, 1327, 1328, 1329, 1330, 1332, 1333, 1334, 1335, 1337, 1338, 1339, 1340, 1341, 1342,
    1343, 1344, 1346, 1347, 1348, 1349, 1351, 1352, 1353, 1354, 1356, 1357, 1358, 1359, 1361, 1362, 1363, 1364, 1366,
    1367, 1368, 1369, 1371, 1372, 1373, 1374, 1376, 1377, 1378, 1379, 1381, 1382, 1383, 1384, 1386, 1387, 1388, 1389,
    1391, 1392, 1393, 1394, 1396, 1397, 1398, 1399, 1401, 1402, 1403, 1404, 1406, 1407, 1408, 1409, 1411, 1412, 1413,
    1414, 1416, 1417, 1418, 1419, 1421, 1422, 1423, 1424, 1426, 1427, 1429, 1430, 1431, 1432, 1434, 1435, 1437, 1438,
    1439, 1440, 1442, 1443, 1444, 1445, 1447, 1448, 1449, 1450, 1452, 1453, 1454, 1455, 1458, 1459, 1460, 1461, 1463,
    1464, 1465, 1466, 1468, 1469, 1471, 1472, 1473, 1474, 1476, 1477, 1479, 1480, 1481, 1482, 1484, 1485, 1486, 1487,
    1489, 1490, 1492, 1493, 1494, 1495, 1497, 1498, 1501, 1502, 1503, 1504, 1506, 1507, 1509, 1510, 1512, 1513, 1514,
    1515, 1517, 1518, 1520, 1521, 1523, 1524, 1525, 1526, 1528, 1529, 1531, 1532, 1534, 1535, 1536, 1537, 1539, 1540,
    1542, 1543, 1545, 1546, 1547, 1548, 1550, 1551, 1553, 1554, 1556, 1557, 1558, 1559, 1561, 1562, 1564, 1565, 1567,
    1568, 1569, 1570, 1572, 1573, 1575, 1576, 1578, 1579, 1580, 1581, 1583, 1584, 1586, 1587, 1590, 1591, 1592, 1593,
    1595, 1596, 1598, 1599, 1601, 1602, 1604, 1605, 1607, 1608, 1609, 1610, 1613, 1614, 1615, 1616, 1618, 1619, 1621,
    1622, 1624, 1625, 1627, 1628, 1630, 1631, 1632, 1633, 1637, 1638, 1639, 1640, 1642, 1643, 1645, 1646, 1648, 1649,
    1651, 1652, 1654, 1655, 1656, 1657, 1660, 1661, 1663, 1664, 1666, 1667, 1669, 1670, 1672, 1673, 1675, 1676, 1678,
    1679, 1681, 1682, 1685, 1686, 1688, 1689, 1691, 1692, 1694, 1695, 1697, 1698, 1700, 1701, 1703, 1704, 1706, 1707,
    1709, 1710, 1712, 1713, 1715, 1716, 1718, 1719, 1721, 1722, 1724, 1725, 1727, 1728, 1730, 1731, 1734, 1735, 1737,
    1738, 1740, 1741, 1743, 1744, 1746, 1748, 1749, 1751, 1752, 1754, 1755, 1757, 1759, 1760, 1762, 1763, 1765, 1766,
    1768, 1769, 1771, 1773, 1774, 1776, 1777, 1779, 1780, 1782, 1785, 1786, 1788, 1789, 1791, 1793, 1794, 1796, 1798,
    1799, 1801, 1802, 1804, 1806, 1807, 1809, 1811, 1812, 1814, 1815, 1817, 1819, 1820, 1822, 1824, 1825, 1827, 1828,
    1830, 1832, 1833, 1835, 1837, 1838, 1840, 1841, 1843, 1845, 1846, 1848, 1850, 1851, 1853, 1854, 1856, 1858, 1859,
    1861, 1864, 1865, 1867, 1868, 1870, 1872, 1873, 1875, 1877, 1879, 1880, 1882, 1884, 1885, 1887, 1888, 1891, 1892,
    1894, 1895, 1897, 1899, 1900, 1902, 1904, 1906, 1907, 1909, 1911, 1912, 1914, 1915, 1918, 1919, 1921, 1923, 1925,
    1926, 1928, 1930, 1932, 1933, 1935, 1937, 1939, 1940, 1942, 1944, 1946, 1947, 1949, 1951, 1953, 1954, 1956, 1958,
    1960, 1961, 1963, 1965, 1967, 1968, 1970, 1972, 1975, 1976, 1978, 1980, 1982, 1983, 1985, 1987, 1989, 1990, 1992,
    1994, 1996, 1997, 1999, 2001, 2003, 2004, 2006, 2008, 2010, 2011, 2013, 2015, 2017, 2019, 2021, 2022, 2024, 2026,
    2028, 2029, 2032, 2033, 2035, 2037, 2039, 2041, 2043, 2044, 2047, 2048, 2050, 2052, 2054, 2056, 2058, 2059, 2062,
    2063, 2065, 2067, 2069, 2071, 2073, 2074, 2077, 2078, 2080, 2082, 2084, 2086, 2088, 2089, 2092, 2093, 2095, 2097,
    2099, 2101, 2103, 2104, 2107, 2108, 2110, 2112, 2114, 2116, 2118, 2119, 2122, 2123, 2125, 2127, 2129, 2131, 2133,
    2134, 2137, 2139, 2141, 2142, 2145, 2146, 2148, 2150, 2153, 2154, 2156, 2158, 2160, 2162, 2164, 2165, 2168, 2170,
    2172, 2173, 2176, 2177, 2179, 2181, 2185, 2186, 2188, 2190, 2192, 2194, 2196, 2197, 2200, 2202, 2204, 2205, 2208,
    2209, 2211, 2213, 2216, 2218, 2220, 2222, 2223, 2226, 2227, 2230, 2232, 2234, 2236, 2238, 2239, 2242, 2243, 2246,
    2249, 2251, 2253, 2255, 2256, 2259, 2260, 2263, 2265, 2267, 2269, 2271, 2272, 2275, 2276, 2279, 2281, 2283, 2285,
    2287, 2288, 2291, 2292, 2295, 2297, 2299, 2301, 2303, 2304, 2307, 2308, 2311, 2315, 2317, 2319, 2321, 2322, 2325,
    2326, 2329, 2331, 2333, 2335, 2337, 2338, 2341, 2342, 2345, 2348, 2350, 2352, 2354, 2355, 2358, 2359, 2362, 2364,
    2366, 2368, 2370, 2371, 2374, 2375, 2378, 2382, 2384, 2386, 2388, 2389, 2392, 2393, 2396, 2398, 2400, 2402, 2404,
    2407, 2410, 2411, 2414, 2417, 2419, 2421, 2423, 2424, 2427, 2428, 2431, 2433, 2435, 2437, 2439, 2442, 2445, 2446,
    2449, 2452, 2454, 2456, 2458, 2459, 2462, 2463, 2466, 2468, 2470, 2472, 2474, 2477, 2480, 2481, 2484, 2488, 2490,
    2492, 2494, 2495, 2498, 2499, 2502, 2504, 2506, 2508, 2510, 2513, 2516, 2517, 2520, 2524, 2526, 2528, 2530, 2531,
    2534, 2535, 2538, 2540, 2542, 2544, 2546, 2549, 2552, 2553, 2556, 2561, 2563, 2565, 2567, 2568, 2571, 2572, 2575,
    2577, 2579, 2581, 2583, 2586, 2589, 2590, 2593};

/* Noise LFO waveform: 256 samples out of much longer data of the real chip
 * (the algorithm is unknown). */
static const uint8_t lfo_noise_waveform[256] = {
    0xFF, 0xEE, 0xD3, 0x80, 0x58, 0xDA, 0x7F, 0x94, 0x9E, 0xE3, 0xFA, 0x00, 0x4D, 0xFA, 0xFF, 0x6A, 0x7A, 0xDE, 0x49,
    0xF6, 0x00, 0x33, 0xBB, 0x63, 0x91, 0x60, 0x51, 0xFF, 0x00, 0xD8, 0x7F, 0xDE, 0xDC, 0x73, 0x21, 0x85, 0xB2, 0x9C,
    0x5D, 0x24, 0xCD, 0x91, 0x9E, 0x76, 0x7F, 0x20, 0xFB, 0xF3, 0x00, 0xA6, 0x3E, 0x42, 0x27, 0x69, 0xAE, 0x33, 0x45,
    0x44, 0x11, 0x41, 0x72, 0x73, 0xDF, 0xA2, 0x32, 0xBD, 0x7E, 0xA8, 0x13, 0xEB, 0xD3, 0x15, 0xDD, 0xFB, 0xC9, 0x9D,
    0x61, 0x2F, 0xBE, 0x9D, 0x23, 0x65, 0x51, 0x6A, 0x84, 0xF9, 0xC9, 0xD7, 0x23, 0xBF, 0x65, 0x19, 0xDC, 0x03, 0xF3,
    0x24, 0x33, 0xB6, 0x1E, 0x57, 0x5C, 0xAC, 0x25, 0x89, 0x4D, 0xC5, 0x9C, 0x99, 0x15, 0x07, 0xCF, 0xBA, 0xC5, 0x9B,
    0x15, 0x4D, 0x8D, 0x2A, 0x1E, 0x1F, 0xEA, 0x2B, 0x2F, 0x64, 0xA9, 0x50, 0x3D, 0xAB, 0x50, 0x77, 0xE9, 0xC0, 0xAC,
    0x6D, 0x3F, 0xCA, 0xCF, 0x71, 0x7D, 0x80, 0xA6, 0xFD, 0xFF, 0xB5, 0xBD, 0x6F, 0x24, 0x7B, 0x00, 0x99, 0x5D, 0xB1,
    0x48, 0xB0, 0x28, 0x7F, 0x80, 0xEC, 0xBF, 0x6F, 0x6E, 0x39, 0x90, 0x42, 0xD9, 0x4E, 0x2E, 0x12, 0x66, 0xC8, 0xCF,
    0x3B, 0x3F, 0x10, 0x7D, 0x79, 0x00, 0xD3, 0x1F, 0x21, 0x93, 0x34, 0xD7, 0x19, 0x22, 0xA2, 0x08, 0x20, 0xB9, 0xB9,
    0xEF, 0x51, 0x99, 0xDE, 0xBF, 0xD4, 0x09, 0x75, 0xE9, 0x8A, 0xEE, 0xFD, 0xE4, 0x4E, 0x30, 0x17, 0xDF, 0xCE, 0x11,
    0xB2, 0x28, 0x35, 0xC2, 0x7C, 0x64, 0xEB, 0x91, 0x5F, 0x32, 0x0C, 0x6E, 0x00, 0xF9, 0x92, 0x19, 0xDB, 0x8F, 0xAB,
    0xAE, 0xD6, 0x12, 0xC4, 0x26, 0x62, 0xCE, 0xCC, 0x0A, 0x03, 0xE7, 0xDD, 0xE2, 0x4D, 0x8A, 0xA6, 0x46, 0x95, 0x0F,
    0x8F, 0xF5, 0x15, 0x97, 0x32, 0xD4, 0x28, 0x1E, 0x55};

/* --- Tables ------------------------------------------------------------- */

static int tables_done;

/* The sine and attenuation tables, as the chip holds them (MAME's
 * init_tables), computed without libm: the attenuation table from 2^(-x/32)
 * by a fixed-point exp2, the sine's "decibels" by a fixed-point log2 of the
 * sine. The chip's rounding of both is kept. */
static uint32_t exp2_frac_q16(uint32_t f) /* 2^(f / 65536) in 16.16, f < 65536 */
{
    /* 2^x = 1 + x ln2 + ...: a 9-term series, accurate to 1e-7 */
    const double x = f * (0.69314718055994531 / 65536.0);
    double term = 1, sum = 1;
    for (int i = 1; i < 10; i++)
    {
        term *= x / i;
        sum += term;
    }
    return (uint32_t)(sum * 65536.0 + 0.5);
}

static double sin_of(int i) /* sin((2i + 1) * pi / 1024) */
{
    /* a Taylor series on the reduced angle: enough for the table's rounding */
    double a = (2 * i + 1) * (3.14159265358979323846 / YM_SIN_LEN);
    double sign = 1;
    if (a > 3.14159265358979323846)
    {
        a -= 3.14159265358979323846;
        sign = -1;
    }
    if (a > 3.14159265358979323846 / 2)
        a = 3.14159265358979323846 - a;
    double term = a, sum = a;
    for (int k = 1; k < 10; k++)
    {
        term *= -a * a / ((2 * k) * (2 * k + 1));
        sum += term;
    }
    return sign * sum;
}

static double log2_of(double v) /* v in (0, 1] */
{
    /* log2 by range reduction and a series on ln((1+y)/(1-y)) */
    int e = 0;
    while (v < 0.5)
    {
        v *= 2;
        e--;
    }
    const double y = (v - 1) / (v + 1);
    double term = y, sum = 0;
    for (int k = 1; k < 30; k += 2)
    {
        sum += term / k;
        term *= y * y;
    }
    return e + 2 * sum / 0.69314718055994531;
}

static void init_tables(void)
{
    if (tables_done)
        return;
    tables_done = 1;
    for (int x = 0; x < YM_TL_RES_LEN; x++)
    {
        /* m = 2^16 / 2^((x + 1) * (ENV_STEP / 4) / 8): 2^(16 - (x+1)/256) */
        const uint32_t frac = (uint32_t)((x + 1) * 256); /* (x+1)/256 in 16.16 */
        /* 2^16 / 2^((x+1)/256), an integer below 2^16; never 2^16 thanks to the (x+1) */
        int n = (int)(((uint64_t)65536 * 65536) / exp2_frac_q16(frac)); /* 16 bits here */
        n >>= 4;                /* 12 bits here */
        if (n & 1)              /* round to closest */
            n = (n >> 1) + 1;
        else
            n = n >> 1;
        n <<= 2; /* 13 bits here (as in real chip) */
        tl_tab[x * 2 + 0] = (int16_t)n;
        tl_tab[x * 2 + 1] = (int16_t)-tl_tab[x * 2 + 0];
        for (int i = 1; i < 13; i++)
        {
            tl_tab[x * 2 + 0 + i * 2 * YM_TL_RES_LEN] = (int16_t)(tl_tab[x * 2 + 0] >> i);
            tl_tab[x * 2 + 1 + i * 2 * YM_TL_RES_LEN] = (int16_t)-tl_tab[x * 2 + 0 + i * 2 * YM_TL_RES_LEN];
        }
    }
    for (int i = 0; i < YM_SIN_LEN; i++)
    {
        /* non-standard sinus, verified on the real chip; never zero */
        const double mm = sin_of(i);
        const double am = mm < 0 ? -mm : mm;
        /* convert to 'decibels': o = 8 * log2(1 / |m|) / (ENV_STEP / 4) */
        double o = 8 * (-log2_of(am));
        o = o / (ENV_STEP / 4);
        int n = (int)(2.0 * o);
        if (n & 1) /* round to closest */
            n = (n >> 1) + 1;
        else
            n = n >> 1;
        sin_tab[i] = (uint16_t)(n * 2 + (mm >= 0.0 ? 0 : 1));
    }
    for (int i = 0; i < 16; i++)
        d1l_tab[i] = (uint32_t)((i != 15 ? i : i + 16) * (4.0 / ENV_STEP)); /* every 3 'dB' except for all bits = 1 = 45+48 'dB' */
}

/* The phase increment for an index into MAME's 11-octave frequency table
 * (kc_i + dt2 + the LFO's offset), scaled to the output rate: octave 0..1
 * are the reference octave 2 shifted down, 3..7 shifted up, -1 the lowest
 * note and 8..9 the highest (MAME's init_tables). */
static inline uint32_t SAL_HOT(ym_freq)(const ym2151_t *ym, uint32_t idx)
{
    uint32_t t = idx / 768; /* 0: octave -1, 1..8: octaves 0..7, 9, 10: above */
    uint32_t i = idx - t * 768;
    if (t == 0)
    {
        t = 1;
        i = 0;
    }
    else if (t >= 9)
    {
        t = 8;
        i = 767;
    }
    /* the reference octave: phaseinc_rom << (FREQ_SH - 10), times the rate scale, in steps of 64 */
    const uint32_t ref = ((phaseinc_rom[i] * ym->freq_scale) >> 10) & 0xffffffc0u;
    const int j = (int)t - 1; /* MAME's octave number */
    if (j < 2)
        return (ref >> (2 - j)) & 0xffffffc0u;
    return ref << (j - 2);
}

/* --- Operators ----------------------------------------------------------- */

static inline void SAL_HOT(key_on)(ym2151_op_t *op, uint32_t key_set, uint32_t eg_cnt)
{
    if (!op->key)
    {
        op->phase = 0;      /* clear phase */
        op->state = EG_ATT; /* KEY ON = attack */
        op->volume += (~op->volume * (eg_inc[op->eg_sel_ar + ((eg_cnt >> op->eg_sh_ar) & 7)])) >> 4;
        if (op->volume <= MIN_ATT_INDEX)
        {
            op->volume = MIN_ATT_INDEX;
            op->state = EG_DEC;
        }
    }
    op->key |= key_set;
}

static inline void SAL_HOT(key_off)(ym2151_op_t *op, uint32_t key_set)
{
    if (op->key)
    {
        op->key &= ~key_set;
        if (!op->key)
        {
            if (op->state > EG_REL)
                op->state = EG_REL; /* KEY OFF = release */
        }
    }
}

static void SAL_HOT(envelope_KONKOFF)(ym2151_t *ym, ym2151_op_t *op, int v)
{
    /* m1, m2, c1, c2 */
    static const uint8_t masks[4] = {0x08, 0x20, 0x10, 0x40};
    for (int i = 0; i != 4; i++)
    {
        if (v & masks[i])
            key_on(&op[i], 1, ym->eg_cnt);
        else
            key_off(&op[i], 1);
    }
}

static void set_connect(ym2151_t *ym, ym2151_op_t *om1, int cha, int v)
{
    ym2151_op_t *om2 = om1 + 1;
    ym2151_op_t *oc1 = om1 + 2;
    /* set connect algorithm; MEM is simply one sample delay */
    switch (v & 7)
    {
    case 0:
        /* M1---C1---MEM---M2---C2---OUT */
        om1->connect = &ym->c1;
        oc1->connect = &ym->mem;
        om2->connect = &ym->c2;
        om1->mem_connect = &ym->m2;
        break;
    case 1:
        /* M1------+-MEM---M2---C2---OUT */
        /*      C1-+                     */
        om1->connect = &ym->mem;
        oc1->connect = &ym->mem;
        om2->connect = &ym->c2;
        om1->mem_connect = &ym->m2;
        break;
    case 2:
        /* M1-----------------+-C2---OUT */
        /*      C1---MEM---M2-+          */
        om1->connect = &ym->c2;
        oc1->connect = &ym->mem;
        om2->connect = &ym->c2;
        om1->mem_connect = &ym->m2;
        break;
    case 3:
        /* M1---C1---MEM------+-C2---OUT */
        /*                 M2-+          */
        om1->connect = &ym->c1;
        oc1->connect = &ym->mem;
        om2->connect = &ym->c2;
        om1->mem_connect = &ym->c2;
        break;
    case 4:
        /* M1---C1-+-OUT */
        /* M2---C2-+     */
        /* MEM: not used */
        om1->connect = &ym->c1;
        oc1->connect = &ym->chanout[cha];
        om2->connect = &ym->c2;
        om1->mem_connect = &ym->mem; /* store it anywhere where it will not be used */
        break;
    case 5:
        /*    +----C1----+     */
        /* M1-+-MEM---M2-+-OUT */
        /*    +----C2----+     */
        om1->connect = NULL; /* special mark */
        oc1->connect = &ym->chanout[cha];
        om2->connect = &ym->chanout[cha];
        om1->mem_connect = &ym->m2;
        break;
    case 6:
        /* M1---C1-+     */
        /*      M2-+-OUT */
        /*      C2-+     */
        /* MEM: not used */
        om1->connect = &ym->c1;
        oc1->connect = &ym->chanout[cha];
        om2->connect = &ym->chanout[cha];
        om1->mem_connect = &ym->mem; /* store it anywhere where it will not be used */
        break;
    case 7:
        /* M1-+     */
        /* C1-+-OUT */
        /* M2-+     */
        /* C2-+     */
        /* MEM: not used*/
        om1->connect = &ym->chanout[cha];
        oc1->connect = &ym->chanout[cha];
        om2->connect = &ym->chanout[cha];
        om1->mem_connect = &ym->mem; /* store it anywhere where it will not be used */
        break;
    }
}

static inline void refresh_op_eg(ym2151_op_t *op, uint32_t kc)
{
    const uint32_t v = kc >> op->ks; /* v = 32 + 2*RATE + RKS = max 126 */
    if ((op->ar + v) < 32 + 62)
    {
        op->eg_sh_ar = eg_rate_shift[op->ar + v];
        op->eg_sel_ar = eg_rate_select[op->ar + v];
    }
    else
    {
        op->eg_sh_ar = 0;
        op->eg_sel_ar = 17 * RATE_STEPS;
    }
    op->eg_sh_d1r = eg_rate_shift[op->d1r + v];
    op->eg_sel_d1r = eg_rate_select[op->d1r + v];
    op->eg_sh_d2r = eg_rate_shift[op->d2r + v];
    op->eg_sel_d2r = eg_rate_select[op->d2r + v];
    op->eg_sh_rr = eg_rate_shift[op->rr + v];
    op->eg_sel_rr = eg_rate_select[op->rr + v];
}

static void refresh_EG(ym2151_op_t *op)
{
    const uint32_t kc = op->kc;
    for (int i = 0; i < 4; i++)
        refresh_op_eg(op + i, kc);
}

static inline uint32_t op_freq(const ym2151_t *ym, const ym2151_op_t *op, uint32_t kc_channel)
{
    return ((ym_freq(ym, kc_channel + op->dt2) + op->dt1) * op->mul) >> 1;
}

/* --- Registers ------------------------------------------------------------ */

static void write_reg(ym2151_t *ym, int r, int v)
{
    ym2151_op_t *op = &ym->oper[(r & 0x07) * 4 + ((r & 0x18) >> 3)];

    r &= 0xff;
    v &= 0xff;
    switch (r & 0xe0)
    {
    case 0x00:
        switch (r)
        {
        case 0x01: /* LFO reset(bit 1), Test Register (other bits) */
            ym->test = (uint8_t)v;
            if (v & 2)
                ym->lfo_phase = 0;
            break;
        case 0x08:
            envelope_KONKOFF(ym, &ym->oper[(v & 7) * 4], v);
            break;
        case 0x0f: /* noise mode enable, noise period */
            ym->noise = (uint32_t)v;
            ym->noise_f = 32 - (v & 0x1f);
            if (ym->noise_f < 2)
                ym->noise_f = 2; /* rate 30 and 31 are the same */
            break;
        case 0x10: /* timer A hi */
            ym->timer_a_index = (ym->timer_a_index & 0x003) | ((uint32_t)v << 2);
            break;
        case 0x11: /* timer A low */
            ym->timer_a_index = (ym->timer_a_index & 0x3fc) | (v & 3);
            break;
        case 0x12: /* timer B */
            ym->timer_b_index = (uint32_t)v;
            break;
        case 0x14: /* CSM, irq flag reset, irq enable, timer start/stop */
            ym->irq_enable = (uint32_t)v; /* bit 3-timer B, bit 2-timer A, bit 7 - CSM */
            if (v & 0x10)             /* reset timer A irq flag */
                ym->status &= ~1u;
            if (v & 0x20) /* reset timer B irq flag */
                ym->status &= ~2u;
            if (v & 0x02)
            {
                /* load and start timer B, only if it was not already started */
                if (!ym->timer_b_on)
                {
                    ym->timer_b_on = 1;
                    ym->timer_b_left = (int32_t)((16 * (256 - ym->timer_b_index)) << 16);
                }
            }
            else
            {
                ym->timer_b_on = 0;
            }
            if (v & 0x01)
            {
                /* load and start timer A, only if it was not already started */
                if (!ym->timer_a_on)
                {
                    ym->timer_a_on = 1;
                    ym->timer_a_left = (int32_t)((1024 - ym->timer_a_index) << 16);
                }
            }
            else
            {
                ym->timer_a_on = 0;
            }
            break;
        case 0x18: /* LFO frequency */
            ym->lfo_overflow = (1u << ((15 - (v >> 4)) + 3)) * (1u << LFO_SH);
            ym->lfo_counter_add = 0x10 + (v & 0x0f);
            break;
        case 0x19: /* PMD (bit 7==1) or AMD (bit 7==0) */
            if (v & 0x80)
                ym->pmd = (int8_t)(v & 0x7f);
            else
                ym->amd = (uint8_t)(v & 0x7f);
            break;
        case 0x1b: /* CT2, CT1, LFO waveform */
            ym->ct = (uint8_t)(v >> 6);
            ym->lfo_wsel = (uint8_t)(v & 3);
            break;
        default:
            break;
        }
        break;

    case 0x20:
        op = &ym->oper[(r & 7) * 4];
        switch (r & 0x18)
        {
        case 0x00: /* RL enable, Feedback, Connection */
            op->fb_shift = ((v >> 3) & 7) ? ((v >> 3) & 7) + 6 : 0;
            ym->pan[(r & 7) * 2] = (v & 0x40) ? ~0u : 0;
            ym->pan[(r & 7) * 2 + 1] = (v & 0x80) ? ~0u : 0;
            ym->connect[r & 7] = (uint8_t)(v & 7);
            set_connect(ym, op, r & 7, v & 7);
            break;
        case 0x08: /* Key Code */
            v &= 0x7f;
            if ((uint32_t)v != op->kc)
            {
                uint32_t kc_channel = (uint32_t)(v - (v >> 2)) * 64;
                kc_channel += 768;
                kc_channel |= (op->kc_i & 63);
                for (int i = 0; i < 4; i++)
                {
                    op[i].kc = (uint32_t)v;
                    op[i].kc_i = kc_channel;
                }
                const uint32_t kc = (uint32_t)v >> 2;
                for (int i = 0; i < 4; i++)
                {
                    op[i].dt1 = ym->dt1_freq[op[i].dt1_i + kc];
                    op[i].freq = op_freq(ym, &op[i], kc_channel);
                }
                refresh_EG(op);
            }
            break;
        case 0x10: /* Key Fraction */
            v >>= 2;
            if ((uint32_t)v != (op->kc_i & 63))
            {
                uint32_t kc_channel = (uint32_t)v;
                kc_channel |= (op->kc_i & ~63u);
                for (int i = 0; i < 4; i++)
                {
                    op[i].kc_i = kc_channel;
                    op[i].freq = op_freq(ym, &op[i], kc_channel);
                }
            }
            break;
        case 0x18: /* PMS, AMS */
            op->pms = (uint32_t)((v >> 4) & 7);
            op->ams = (uint32_t)(v & 3);
            break;
        }
        break;

    case 0x40: /* DT1, MUL */
    {
        const uint32_t olddt1_i = op->dt1_i;
        const uint32_t oldmul = op->mul;
        op->dt1_i = (uint32_t)((v & 0x70) << 1);
        op->mul = (v & 0x0f) ? (uint32_t)((v & 0x0f) << 1) : 1;
        if (olddt1_i != op->dt1_i)
            op->dt1 = ym->dt1_freq[op->dt1_i + (op->kc >> 2)];
        if ((olddt1_i != op->dt1_i) || (oldmul != op->mul))
            op->freq = op_freq(ym, op, op->kc_i);
    }
    break;

    case 0x60: /* TL */
        op->tl = (uint32_t)((v & 0x7f) << (ENV_BITS - 7)); /* 7bit TL */
        break;

    case 0x80: /* KS, AR */
    {
        const uint32_t oldks = op->ks;
        const uint32_t oldar = op->ar;
        op->ks = (uint32_t)(5 - (v >> 6));
        op->ar = (v & 0x1f) ? 32 + (uint32_t)((v & 0x1f) << 1) : 0;
        if ((op->ar != oldar) || (op->ks != oldks))
        {
            if ((op->ar + (op->kc >> op->ks)) < 32 + 62)
            {
                op->eg_sh_ar = eg_rate_shift[op->ar + (op->kc >> op->ks)];
                op->eg_sel_ar = eg_rate_select[op->ar + (op->kc >> op->ks)];
            }
            else
            {
                op->eg_sh_ar = 0;
                op->eg_sel_ar = 17 * RATE_STEPS;
            }
        }
        if (op->ks != oldks)
        {
            op->eg_sh_d1r = eg_rate_shift[op->d1r + (op->kc >> op->ks)];
            op->eg_sel_d1r = eg_rate_select[op->d1r + (op->kc >> op->ks)];
            op->eg_sh_d2r = eg_rate_shift[op->d2r + (op->kc >> op->ks)];
            op->eg_sel_d2r = eg_rate_select[op->d2r + (op->kc >> op->ks)];
            op->eg_sh_rr = eg_rate_shift[op->rr + (op->kc >> op->ks)];
            op->eg_sel_rr = eg_rate_select[op->rr + (op->kc >> op->ks)];
        }
    }
    break;

    case 0xa0: /* LFO AM enable, D1R */
        op->AMmask = (v & 0x80) ? ~0u : 0;
        op->d1r = (v & 0x1f) ? 32 + (uint32_t)((v & 0x1f) << 1) : 0;
        op->eg_sh_d1r = eg_rate_shift[op->d1r + (op->kc >> op->ks)];
        op->eg_sel_d1r = eg_rate_select[op->d1r + (op->kc >> op->ks)];
        break;

    case 0xc0: /* DT2, D2R */
    {
        const uint32_t olddt2 = op->dt2;
        op->dt2 = dt2_tab[v >> 6];
        if (op->dt2 != olddt2)
            op->freq = op_freq(ym, op, op->kc_i);
    }
        op->d2r = (v & 0x1f) ? 32 + (uint32_t)((v & 0x1f) << 1) : 0;
        op->eg_sh_d2r = eg_rate_shift[op->d2r + (op->kc >> op->ks)];
        op->eg_sel_d2r = eg_rate_select[op->d2r + (op->kc >> op->ks)];
        break;

    case 0xe0: /* D1L, RR */
        op->d1l = d1l_tab[v >> 4];
        op->rr = 34 + (uint32_t)((v & 0x0f) << 2);
        op->eg_sh_rr = eg_rate_shift[op->rr + (op->kc >> op->ks)];
        op->eg_sel_rr = eg_rate_select[op->rr + (op->kc >> op->ks)];
        break;
    }
}

void ym2151_write(ym2151_t *ym, int a0, uint8_t data)
{
    if (a0 & 1)
        write_reg(ym, ym->lastreg, data);
    else
        ym->lastreg = data;
}

uint8_t ym2151_read(ym2151_t *ym, int a0)
{
    if (a0 & 1)
        return (uint8_t)ym->status;
    return 0xff; /* confirmed on a real YM2151 */
}

/* --- Life cycle ------------------------------------------------------------ */

void ym2151_init(ym2151_t *ym, uint32_t clock, uint32_t samplerate)
{
    init_tables();
    memset(ym, 0, sizeof(*ym));
    /* the chip's rate (clock / 64) over the output rate, 16.16 */
    ym->freq_scale = (uint32_t)(((uint64_t)(clock / 64) << 16) / samplerate);
    ym->chip_per_out = ym->freq_scale;
    for (int j = 0; j < 4; j++)
    {
        for (int i = 0; i < 32; i++)
        {
            /* the phase increment of the detune in Hz: dt1_tab * clock / 64 /
             * 2^20, times SIN_LEN over the output rate */
            const int32_t f = (int32_t)(((uint32_t)dt1_tab[j * 32 + i] * ym->freq_scale) >> 10);
            ym->dt1_freq[(j + 0) * 32 + i] = f;
            ym->dt1_freq[(j + 4) * 32 + i] = -f;
        }
    }
    ym->lfo_timer_add = ym->freq_scale >> (16 - LFO_SH);
    ym->eg_timer_add = ym->freq_scale;
    ym->eg_timer_overflow = 3 << EG_SH;
    ym->noise_add = 2 * ym->freq_scale;
    ym2151_reset(ym);
}

void ym2151_reset(ym2151_t *ym)
{
    for (int i = 0; i < 32; i++)
    {
        memset(&ym->oper[i], 0, sizeof(ym2151_op_t));
        ym->oper[i].volume = MAX_ATT_INDEX;
        ym->oper[i].kc_i = 768; /* min kc_i value */
    }
    ym->eg_timer = 0;
    ym->eg_cnt = 0;
    ym->lfo_timer = 0;
    ym->lfo_counter = 0;
    ym->lfo_phase = 0;
    ym->lfo_wsel = 0;
    ym->pmd = 0;
    ym->amd = 0;
    ym->lfa = 0;
    ym->lfp = 0;
    ym->test = 0;
    ym->irq_enable = 0;
    ym->timer_a_on = ym->timer_b_on = 0;
    ym->timer_a_index = ym->timer_b_index = 0;
    ym->noise = 0;
    ym->noise_rng = 0;
    ym->noise_p = 0;
    ym->noise_f = 32;
    ym->csm_req = 0;
    ym->status = 0;
    ym->lastreg = 0;
    write_reg(ym, 0x1b, 0); /* only because of CT1, CT2 output pins */
    write_reg(ym, 0x18, 0); /* set LFO frequency */
    for (int i = 0x20; i < 0x100; i++) /* set the operators */
        write_reg(ym, i, 0);
}

/* --- Samples --------------------------------------------------------------- */

static inline int32_t SAL_HOT(op_calc)(const ym2151_op_t *OP, uint32_t env, int32_t pm)
{
    const uint32_t p =
        (env << 3) + sin_tab[(((int32_t)((OP->phase & ~(uint32_t)FREQ_MASK) + ((uint32_t)pm << 15))) >> FREQ_SH) & YM_SIN_MASK];
    if (p >= YM_TL_TAB_LEN)
        return 0;
    return tl_tab[p];
}

static inline int32_t SAL_HOT(op_calc1)(const ym2151_op_t *OP, uint32_t env, int32_t pm)
{
    const int32_t i = (int32_t)(OP->phase & ~(uint32_t)FREQ_MASK) + pm;
    const uint32_t p = (env << 3) + sin_tab[(i >> FREQ_SH) & YM_SIN_MASK];
    if (p >= YM_TL_TAB_LEN)
        return 0;
    return tl_tab[p];
}

#define volume_calc(OP) ((OP)->tl + ((uint32_t)(OP)->volume) + (AM & (OP)->AMmask))

static void SAL_HOT(chan_calc)(ym2151_t *ym, unsigned chan)
{
    ym2151_op_t *op;
    uint32_t env;
    uint32_t AM = 0;

    ym->m2 = ym->c1 = ym->c2 = ym->mem = 0;
    op = &ym->oper[chan * 4];          /* M1 */
    *op->mem_connect = op->mem_value; /* restore delayed sample (MEM) value to m2 or c2 */

    if (op->ams)
        AM = ym->lfa << (op->ams - 1);
    env = volume_calc(op);
    {
        int32_t out = op->fb_out_prev + op->fb_out_curr;
        op->fb_out_prev = op->fb_out_curr;
        if (!op->connect)
            /* algorithm 5 */
            ym->mem = ym->c1 = ym->c2 = op->fb_out_prev;
        else
            /* other algorithms */
            *op->connect = op->fb_out_prev;
        op->fb_out_curr = 0;
        if (env < ENV_QUIET)
        {
            if (!op->fb_shift)
                out = 0;
            op->fb_out_curr = op_calc1(op, env, (out << op->fb_shift));
        }
    }
    env = volume_calc(op + 1); /* M2 */
    if (env < ENV_QUIET)
        *(op + 1)->connect += op_calc(op + 1, env, ym->m2);
    env = volume_calc(op + 2); /* C1 */
    if (env < ENV_QUIET)
        *(op + 2)->connect += op_calc(op + 2, env, ym->c1);
    env = volume_calc(op + 3); /* C2 */
    if (env < ENV_QUIET)
        ym->chanout[chan] += op_calc(op + 3, env, ym->c2);
    /* M1 */
    op->mem_value = ym->mem;
}

static void SAL_HOT(chan7_calc)(ym2151_t *ym)
{
    ym2151_op_t *op;
    uint32_t env;
    uint32_t AM = 0;

    ym->m2 = ym->c1 = ym->c2 = ym->mem = 0;
    op = &ym->oper[7 * 4];             /* M1 */
    *op->mem_connect = op->mem_value; /* restore delayed sample (MEM) value to m2 or c2 */

    if (op->ams)
        AM = ym->lfa << (op->ams - 1);
    env = volume_calc(op);
    {
        int32_t out = op->fb_out_prev + op->fb_out_curr;
        op->fb_out_prev = op->fb_out_curr;
        if (!op->connect)
            /* algorithm 5 */
            ym->mem = ym->c1 = ym->c2 = op->fb_out_prev;
        else
            /* other algorithms */
            *op->connect = op->fb_out_prev;
        op->fb_out_curr = 0;
        if (env < ENV_QUIET)
        {
            if (!op->fb_shift)
                out = 0;
            op->fb_out_curr = op_calc1(op, env, (out << op->fb_shift));
        }
    }
    env = volume_calc(op + 1); /* M2 */
    if (env < ENV_QUIET)
        *(op + 1)->connect += op_calc(op + 1, env, ym->m2);
    env = volume_calc(op + 2); /* C1 */
    if (env < ENV_QUIET)
        *(op + 2)->connect += op_calc(op + 2, env, ym->c1);
    env = volume_calc(op + 3); /* C2 */
    if (ym->noise & 0x80)
    {
        uint32_t noiseout = 0;
        if (env < 0x3ff)
            noiseout = (env ^ 0x3ff) * 2; /* range of the YM2151 noise output is -2044 to 2040 */
        ym->chanout[7] += (ym->noise_rng & 0x10000) ? (int32_t)noiseout : -(int32_t)noiseout; /* bit 16 -> output */
    }
    else
    {
        if (env < ENV_QUIET)
            ym->chanout[7] += op_calc(op + 3, env, ym->c2);
    }
    /* M1 */
    op->mem_value = ym->mem;
}

static void SAL_HOT(advance_eg)(ym2151_t *ym)
{
    ym->eg_timer += ym->eg_timer_add;
    while (ym->eg_timer >= ym->eg_timer_overflow)
    {
        ym->eg_timer -= ym->eg_timer_overflow;
        ym->eg_cnt++;
        const uint32_t eg_cnt = ym->eg_cnt;
        ym2151_op_t *op = &ym->oper[0]; /* CH 0 M1 */
        unsigned i = 32;
        do
        {
            switch (op->state)
            {
            case EG_ATT: /* attack phase */
                if (!(eg_cnt & ((1u << op->eg_sh_ar) - 1)))
                {
                    op->volume += (~op->volume * (eg_inc[op->eg_sel_ar + ((eg_cnt >> op->eg_sh_ar) & 7)])) >> 4;
                    if (op->volume <= MIN_ATT_INDEX)
                    {
                        op->volume = MIN_ATT_INDEX;
                        op->state = EG_DEC;
                    }
                }
                break;
            case EG_DEC: /* decay phase */
                if (!(eg_cnt & ((1u << op->eg_sh_d1r) - 1)))
                {
                    op->volume += eg_inc[op->eg_sel_d1r + ((eg_cnt >> op->eg_sh_d1r) & 7)];
                    if ((uint32_t)op->volume >= op->d1l)
                        op->state = EG_SUS;
                }
                break;
            case EG_SUS: /* sustain phase */
                if (!(eg_cnt & ((1u << op->eg_sh_d2r) - 1)))
                {
                    op->volume += eg_inc[op->eg_sel_d2r + ((eg_cnt >> op->eg_sh_d2r) & 7)];
                    if (op->volume >= MAX_ATT_INDEX)
                    {
                        op->volume = MAX_ATT_INDEX;
                        op->state = EG_OFF;
                    }
                }
                break;
            case EG_REL: /* release phase */
                if (!(eg_cnt & ((1u << op->eg_sh_rr) - 1)))
                {
                    op->volume += eg_inc[op->eg_sel_rr + ((eg_cnt >> op->eg_sh_rr) & 7)];
                    if (op->volume >= MAX_ATT_INDEX)
                    {
                        op->volume = MAX_ATT_INDEX;
                        op->state = EG_OFF;
                    }
                }
                break;
            }
            op++;
            i--;
        } while (i);
    }
}

static void SAL_HOT(advance)(ym2151_t *ym)
{
    ym2151_op_t *op;
    unsigned i;
    int a, p;

    /* LFO */
    if (ym->test & 2)
    {
        ym->lfo_phase = 0;
    }
    else
    {
        ym->lfo_timer += ym->lfo_timer_add;
        if (ym->lfo_timer >= ym->lfo_overflow)
        {
            ym->lfo_timer -= ym->lfo_overflow;
            ym->lfo_counter += ym->lfo_counter_add;
            ym->lfo_phase += (ym->lfo_counter >> 4);
            ym->lfo_phase &= 255;
            ym->lfo_counter &= 15;
        }
    }
    i = ym->lfo_phase;
    /* calculate LFO AM and PM waveform value (all verified on real chip, except for noise algorithm which is impossible to analyse)*/
    switch (ym->lfo_wsel)
    {
    case 0:
        /* saw */
        /* AM: 255 down to 0 */
        /* PM: 0 to 127, -127 to 0 (at PMD=127: LFP = 0 to 126, -126 to 0) */
        a = 255 - (int)i;
        if (i < 128)
            p = (int)i;
        else
            p = (int)i - 255;
        break;
    case 1:
        /* square */
        /* AM: 255, 0 */
        /* PM: 128,-128 (LFP = exactly +PMD, -PMD) */
        if (i < 128)
        {
            a = 255;
            p = 128;
        }
        else
        {
            a = 0;
            p = -128;
        }
        break;
    case 2:
        /* triangle */
        /* AM: 255 down to 1 step -2; 0 up to 254 step +2 */
        /* PM: 0 to 126 step +2, 127 to 1 step -2, 0 to -126 step -2, -127 to -1 step +2*/
        if (i < 128)
            a = 255 - ((int)i * 2);
        else
            a = ((int)i * 2) - 256;
        if (i < 64)           /* i = 0..63 */
            p = (int)i * 2;   /* 0 to 126 step +2 */
        else if (i < 128)     /* i = 64..127 */
            p = 255 - (int)i * 2; /* 127 to 1 step -2 */
        else if (i < 192)     /* i = 128..191 */
            p = 256 - (int)i * 2; /* 0 to -126 step -2*/
        else                  /* i = 192..255 */
            p = (int)i * 2 - 511; /*-127 to -1 step +2*/
        break;
    case 3:
    default:
        /* random: a snapshot of data from the real chip */
        /* AM: range 0 to 255    */
        /* PM: range -128 to 127 */
        a = lfo_noise_waveform[i];
        p = a - 128;
        break;
    }
    ym->lfa = (uint32_t)(a * ym->amd / 128);
    ym->lfp = p * ym->pmd / 128;

    /*  The Noise Generator of the YM2151 is 17-bit shift register.
     *   Input to the bit16 is negated (bit0 XOR bit3) (EXNOR).
     *   Output of the register is negated (bit0 XOR bit3).
     *   Simply use bit16 as the noise output.
     */
    ym->noise_p += ym->noise_add; /* 32 clocks per noise (2 * the chip's sample rate), scaled */
    while (ym->noise_p >= (ym->noise_f << 16))
    {
        const uint32_t j = ((ym->noise_rng ^ (ym->noise_rng >> 3)) & 1) ^ 1;
        ym->noise_rng = (j << 16) | (ym->noise_rng >> 1);
        ym->noise_p -= ym->noise_f << 16;
    }

    /* phase generator */
    op = &ym->oper[0]; /* CH 0 M1 */
    i = 8;
    do
    {
        if (op->pms) /* only when phase modulation from LFO is enabled for this channel */
        {
            int32_t mod_ind = ym->lfp; /* -128..+127 (8bits signed) */
            if (op->pms < 6)
                mod_ind >>= (6 - op->pms);
            else
                mod_ind <<= (op->pms - 5);
            if (mod_ind)
            {
                const uint32_t kc_channel = op->kc_i + (uint32_t)mod_ind;
                for (int k = 0; k < 4; k++)
                    op[k].phase += op_freq(ym, &op[k], kc_channel);
            }
            else /* phase modulation from LFO is equal to zero */
            {
                for (int k = 0; k < 4; k++)
                    op[k].phase += op[k].freq;
            }
        }
        else /* phase modulation from LFO is disabled */
        {
            for (int k = 0; k < 4; k++)
                op[k].phase += op[k].freq;
        }
        op += 4;
        i--;
    } while (i);

    /* CSM is calculated *after* the phase generator calculations (verified on
     * real chip). The CSM keyon line seems to be ORed with the KO line inside
     * of the chip: it only works when KO (register 0x08) is off. */
    if (ym->csm_req) /* CSM KEYON/KEYOFF sequence request */
    {
        if (ym->csm_req == 2) /* KEY ON */
        {
            op = &ym->oper[0]; /* CH 0 M1 */
            i = 32;
            do
            {
                key_on(op, 2, ym->eg_cnt);
                op++;
                i--;
            } while (i);
            ym->csm_req = 1;
        }
        else /* KEY OFF */
        {
            op = &ym->oper[0]; /* CH 0 M1 */
            i = 32;
            do
            {
                key_off(op, 2);
                op++;
                i--;
            } while (i);
            ym->csm_req = 0;
        }
    }
}

/* The timers, in the chip's samples. */
static inline void SAL_HOT(advance_timers)(ym2151_t *ym)
{
    if (ym->timer_a_on)
    {
        ym->timer_a_left -= (int32_t)ym->chip_per_out;
        if (ym->timer_a_left <= 0)
        {
            ym->timer_a_left += (int32_t)((1024 - ym->timer_a_index) << 16);
            if (ym->irq_enable & 0x80)
                ym->csm_req = 2; /* request KEY ON / KEY OFF sequence */
            if (ym->irq_enable & 0x04)
                ym->status |= 1;
        }
    }
    if (ym->timer_b_on)
    {
        ym->timer_b_left -= (int32_t)ym->chip_per_out;
        if (ym->timer_b_left <= 0)
        {
            ym->timer_b_left += (int32_t)((16 * (256 - ym->timer_b_index)) << 16);
            if (ym->irq_enable & 0x08)
                ym->status |= 2;
        }
    }
}

void SAL_HOT(ym2151_render)(ym2151_t *ym, int32_t *l, int32_t *r, int n)
{
    for (int s = 0; s < n; s++)
    {
        advance_eg(ym);
        for (int ch = 0; ch < 8; ch++)
            ym->chanout[ch] = 0;
        for (unsigned ch = 0; ch < 7; ch++)
            chan_calc(ym, ch);
        chan7_calc(ym);
        int32_t outl = 0, outr = 0;
        for (int ch = 0; ch < 8; ch++)
        {
            outl += ym->chanout[ch] & (int32_t)ym->pan[2 * ch];
            outr += ym->chanout[ch] & (int32_t)ym->pan[2 * ch + 1];
        }
        l[s] += outl;
        r[s] += outr;
        advance(ym);
        advance_timers(ym);
    }
}
