/* QR codes (ISO/IEC 18004) for the App Store's phone page (netupdate.c): byte mode, error
 * correction level M, the smallest version up to QR_VER that holds the data, the mask of least
 * penalty (or a chosen one). After Project Nayuki's QR Code generator (MIT), as the card apps'
 * internal/qr is, so both draw the same code for the same text. Out comes what addon.c's QR page
 * reads from a status file: each row as (size + 3) / 4 hex digits, the leftmost module the high bit
 * of the first. */
#include "peq.h"

#define QR_VER 8 /* 49 modules: the most addon.c's QR page draws (QR_HEX) */
#define QR_N (QR_VER * 4 + 17)
#define QR_WORDS 242 /* version 8's codewords, data and error correction */
#define QR_BLOCK 71  /* the longest block and a short one's gap: version 3's one of 70 */

/* Level M's error correction codewords per block, and blocks, by version (0 unused). */
static const unsigned char ecc_per[QR_VER + 1] = { 0, 10, 16, 26, 18, 24, 16, 18, 22 };
static const unsigned char ecc_blocks[QR_VER + 1] = { 0, 1, 1, 1, 2, 2, 4, 4, 4 };

typedef struct {
    int ver, size;
    unsigned char m[QR_N][QR_N], fn[QR_N][QR_N]; /* dark; a function module, never data or masked */
} qr_t;

static int raw_modules(int ver) {
    int n = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        int align = ver / 7 + 2;
        n -= (25 * align - 10) * align - 55;
        if (ver >= 7) n -= 36;
    }
    return n;
}

static int data_words(int ver) { return raw_modules(ver) / 8 - ecc_per[ver] * ecc_blocks[ver]; }

static void put(qr_t *q, int x, int y, int dark) {
    q->m[y][x] = (unsigned char)dark;
    q->fn[y][x] = 1;
}

static int dist(int dx, int dy) {
    dx = dx < 0 ? -dx : dx, dy = dy < 0 ? -dy : dy;
    return dx > dy ? dx : dy;
}

static void format_bits(qr_t *q, int mask) {
    int data = 0 << 3 | mask, rem = data; /* level M's two bits are 0 */
    for (int i = 0; i < 10; ++i) rem = rem << 1 ^ (rem >> 9) * 0x537;
    int bits = (data << 10 | rem) ^ 0x5412, n = q->size;
    for (int i = 0; i <= 5; ++i) put(q, 8, i, bits >> i & 1);
    put(q, 8, 7, bits >> 6 & 1);
    put(q, 8, 8, bits >> 7 & 1);
    put(q, 7, 8, bits >> 8 & 1);
    for (int i = 9; i < 15; ++i) put(q, 14 - i, 8, bits >> i & 1);
    for (int i = 0; i < 8; ++i) put(q, n - 1 - i, 8, bits >> i & 1);
    for (int i = 8; i < 15; ++i) put(q, 8, n - 15 + i, bits >> i & 1);
    put(q, 8, n - 8, 1);
}

/* The alignment patterns' centres along one axis; their count. */
static int alignments(int ver, int *pos) {
    if (ver == 1) return 0;
    int n = ver / 7 + 2, step = (ver * 8 + n * 3 + 5) / (n * 4 - 4) * 2;
    pos[0] = 6;
    for (int i = n - 1, p = ver * 4 + 17 - 7; i >= 1; --i, p -= step) pos[i] = p;
    return n;
}

static void function_patterns(qr_t *q) {
    int n = q->size, pos[7], k = alignments(q->ver, pos);
    for (int i = 0; i < n; ++i) put(q, 6, i, i % 2 == 0), put(q, i, 6, i % 2 == 0);
    static const signed char finder[3][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 } };
    for (int f = 0; f < 3; ++f) {
        int cx = finder[f][0] ? n - 4 : 3, cy = finder[f][1] ? n - 4 : 3;
        for (int dy = -4; dy <= 4; ++dy)
            for (int dx = -4; dx <= 4; ++dx) {
                int x = cx + dx, y = cy + dy, d = dist(dx, dy);
                if (x >= 0 && x < n && y >= 0 && y < n) put(q, x, y, d != 2 && d != 4);
            }
    }
    for (int i = 0; i < k; ++i)
        for (int j = 0; j < k; ++j) {
            if ((i == 0 && j == 0) || (i == 0 && j == k - 1) || (i == k - 1 && j == 0)) continue;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    put(q, pos[i] + dx, pos[j] + dy, dist(dx, dy) != 1);
        }
    format_bits(q, 0); /* reserved now, drawn for real once the mask is known */
    if (q->ver >= 7) {
        int rem = q->ver;
        for (int i = 0; i < 12; ++i) rem = rem << 1 ^ (rem >> 11) * 0x1f25;
        int v = q->ver << 12 | rem;
        for (int i = 0; i < 18; ++i) {
            int a = n - 11 + i % 3, b = i / 3;
            put(q, a, b, v >> i & 1);
            put(q, b, a, v >> i & 1);
        }
    }
}

static unsigned char gf_mul(unsigned x, unsigned y) {
    unsigned z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (z << 1) ^ (z >> 7) * 0x11d;
        z ^= (y >> i & 1) * x;
    }
    return (unsigned char)z;
}

/* The data codewords split into the version's blocks, each with its error correction, interleaved
 * into out (raw_modules / 8 of them). */
static void interleave(int ver, const unsigned char *data, unsigned char *out) {
    int blocks = ecc_blocks[ver], ecc = ecc_per[ver], raw = raw_modules(ver) / 8,
        shorts = blocks - raw % blocks, short_len = raw / blocks, o = 0;
    unsigned char div[30] = { 0 }, all[4][QR_BLOCK], root = 1;
    div[ecc - 1] = 1;
    for (int i = 0; i < ecc; ++i) { /* the generator polynomial, leading term left out */
        for (int j = 0; j < ecc; ++j)
            div[j] = gf_mul(div[j], root) ^ (j + 1 < ecc ? div[j + 1] : 0);
        root = gf_mul(root, 2);
    }
    for (int i = 0, k = 0; i < blocks; ++i) {
        int n = short_len - ecc + (i >= shorts);
        unsigned char *b = all[i], *e = b + short_len - ecc + 1; /* a short block's gap, then ecc */
        memset(b, 0, short_len + 1);
        memcpy(b, data + k, n);
        k += n;
        for (int d = 0; d < n; ++d) {
            unsigned char f = b[d] ^ e[0];
            memmove(e, e + 1, ecc - 1);
            e[ecc - 1] = 0;
            for (int j = 0; j < ecc; ++j) e[j] ^= gf_mul(div[j], f);
        }
    }
    for (int i = 0; i <= short_len; ++i)
        for (int j = 0; j < blocks; ++j)
            if (i != short_len - ecc || j >= shorts) out[o++] = all[j][i];
}

static void place(qr_t *q, const unsigned char *data, int words) {
    int i = 0, n = q->size;
    for (int right = n - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < n; ++vert)
            for (int j = 0; j < 2; ++j) {
                int x = right - j, y = (right + 1) & 2 ? vert : n - 1 - vert;
                if (!q->fn[y][x] && i < words * 8) {
                    q->m[y][x] = data[i >> 3] >> (7 - (i & 7)) & 1;
                    ++i;
                }
            }
    }
}

static void apply_mask(qr_t *q, int m) {
    for (int y = 0; y < q->size; ++y)
        for (int x = 0; x < q->size; ++x) {
            int inv = m == 0   ? (x + y) % 2 == 0
                      : m == 1 ? y % 2 == 0
                      : m == 2 ? x % 3 == 0
                      : m == 3 ? (x + y) % 3 == 0
                      : m == 4 ? (x / 3 + y / 2) % 2 == 0
                      : m == 5 ? x * y % 2 + x * y % 3 == 0
                      : m == 6 ? (x * y % 2 + x * y % 3) % 2 == 0
                               : ((x + y) % 2 + x * y % 3) % 2 == 0;
            if (inv && !q->fn[y][x]) q->m[y][x] ^= 1;
        }
}

static void history(int size, int run, int *h) {
    if (!h[0]) run += size; /* the light border before the first run */
    memmove(h + 1, h, 6 * sizeof *h);
    h[0] = run;
}

static int finders(const int *h) {
    int n = h[1], core = n > 0 && h[2] == n && h[3] == n * 3 && h[4] == n && h[5] == n;
    return (core && h[0] >= n * 4 && h[6] >= n) + (core && h[6] >= n * 4 && h[0] >= n);
}

/* One row (vertical: a column) i's penalty: runs of five or more, and finder-like patterns. */
static int line(const qr_t *q, int i, int vertical) {
    int p = 0, color = 0, run = 0, h[7] = { 0 }, n = q->size;
    for (int k = 0; k < n; ++k) {
        int v = vertical ? q->m[k][i] : q->m[i][k];
        if (v == color) {
            if (++run == 5)
                p += 3;
            else if (run > 5)
                ++p;
        } else {
            history(n, run, h);
            if (!color) p += finders(h) * 40;
            color = v, run = 1;
        }
    }
    if (color) history(n, run, h), run = 0;
    history(n, run + n, h);
    return p + finders(h) * 40;
}

static int penalty(const qr_t *q) {
    int p = 0, dark = 0, n = q->size, total = n * n;
    for (int i = 0; i < n; ++i) p += line(q, i, 0) + line(q, i, 1);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            int v = q->m[y][x];
            dark += v;
            if (x < n - 1 && y < n - 1 && v == q->m[y][x + 1] && v == q->m[y + 1][x] &&
                v == q->m[y + 1][x + 1])
                p += 3;
        }
    int k = ((dark * 20 - total * 10 < 0 ? total * 10 - dark * 20 : dark * 20 - total * 10) +
             total - 1) /
                total -
            1;
    return p + k * 10;
}

/* data's n bytes as a code with mask (0-7; -1 chooses), into hex (hn bytes, NUL-terminated): its
 * size in modules, 0 when the data or the code doesn't fit. */
int qr_hex(const char *data, unsigned n, int mask, char *hex, unsigned hn) {
    unsigned char words[QR_WORDS] = { 0 }, all[QR_WORDS];
    int ver = 1;
    while (ver <= QR_VER && 4 + 8 + 8 * (int)n > data_words(ver) * 8) ++ver;
    if (ver > QR_VER) return 0;
    int cap = data_words(ver) * 8, bit = 0;
    /* mode 4 (byte), the count, the data; the terminator and padding to a byte are the zeros left
     */
    unsigned head = 4u << 8 | n;
    for (int i = 11; i >= 0; --i, ++bit) words[bit >> 3] |= (head >> i & 1) << (7 - (bit & 7));
    for (unsigned k = 0; k < n; ++k)
        for (int i = 7; i >= 0; --i, ++bit)
            words[bit >> 3] |= ((unsigned char)data[k] >> i & 1) << (7 - (bit & 7));
    bit = bit + 4 < cap ? bit + 4 : cap;
    for (int w = (bit + 7) / 8, pad = 0xec; w < cap / 8; ++w, pad ^= 0xec ^ 0x11)
        words[w] = (unsigned char)pad;
    qr_t *q = calloc(1, sizeof *q);
    if (!q) return 0;
    q->ver = ver, q->size = ver * 4 + 17;
    int size = q->size, w = (size + 3) / 4;
    if ((unsigned)(w * size) >= hn) {
        free(q);
        return 0;
    }
    function_patterns(q);
    interleave(ver, words, all);
    place(q, all, raw_modules(ver) / 8);
    for (int m = 0, best = 1 << 30; mask < 0 && m <= 8; ++m) {
        if (m == 8) { /* the best one, chosen */
            mask = best & 7;
            break;
        }
        apply_mask(q, m);
        format_bits(q, m);
        int p = penalty(q);
        if (p < best >> 3) best = p << 3 | m;
        apply_mask(q, m); /* undone: an XOR */
    }
    apply_mask(q, mask & 7);
    format_bits(q, mask & 7);
    char *o = hex;
    for (int y = 0; y < size; ++y)
        for (int d = 0; d < w; ++d) {
            int v = 0;
            for (int b = 0; b < 4; ++b)
                if (d * 4 + b < size && q->m[y][d * 4 + b]) v |= 8 >> b;
            *o++ = "0123456789abcdef"[v];
        }
    *o = 0;
    free(q);
    return size;
}
