/* Keccak-256 PoW miner for arbitrary prefix strings.
 * Finds a decimal nonce such that keccak256(prefix ++ decimal_nonce)
 * has at least TARGET leading zero bits.
 *
 * The prefix's full 136-byte blocks are absorbed once; only the tail block
 * is rebuilt per iteration, so the hot loop runs a single keccak-f[1600].
 *
 * usage: strmine <prefix> <target_bits> <start_nonce> <stride> [budget_seconds]
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef uint64_t u64;
typedef uint8_t u8;

static const u64 RC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL,
    0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL,
    0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL,
    0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL};
static const int ROTC[24] = {1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
                             27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44};
static const int PILN[24] = {10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
                             15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1};

#define ROL(x, n) (((x) << (n)) | ((x) >> (64 - (n))))

static void keccakf(u64 st[25]) {
    u64 bc[5], t;
    for (int r = 0; r < 24; r++) {
        for (int i = 0; i < 5; i++)
            bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
        for (int i = 0; i < 5; i++) {
            t = bc[(i + 4) % 5] ^ ROL(bc[(i + 1) % 5], 1);
            for (int j = 0; j < 25; j += 5)
                st[j + i] ^= t;
        }
        t = st[1];
        for (int i = 0; i < 24; i++) {
            int j = PILN[i];
            bc[0] = st[j];
            st[j] = ROL(t, ROTC[i]);
            t = bc[0];
        }
        for (int j = 0; j < 25; j += 5) {
            for (int i = 0; i < 5; i++) bc[i] = st[j + i];
            for (int i = 0; i < 5; i++)
                st[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
        }
        st[0] ^= RC[r];
    }
}

#define RATE 136

/* Absorb one 136-byte block into the state. */
static void absorb_block(u64 st[25], const u8 *b) {
    for (int i = 0; i < RATE / 8; i++) {
        u64 v = 0;
        memcpy(&v, b + i * 8, 8);
        st[i] ^= v;
    }
    keccakf(st);
}

static int leading_zeros(const u8 h[32]) {
    int n = 0;
    for (int i = 0; i < 32; i++) {
        if (h[i] == 0) {
            n += 8;
            continue;
        }
        for (int b = 7; b >= 0; b--) {
            if (h[i] & (1u << b)) return n;
            n++;
        }
        return n;
    }
    return n;
}

static u64 now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

static volatile int g_found = 0;
static volatile u64 g_nonce = 0;
static volatile int g_zeros = 0;
static volatile u64 g_hashes = 0;

typedef struct {
    u64 base[25];      /* state after absorbing the prefix's full blocks */
    u8 tail[RATE];     /* prefix remainder, nonce appended per iteration */
    int tail_len;
    int target;
    u64 start, stride, deadline;
} work_t;

static void *worker(void *arg) {
    work_t *w = (work_t *)arg;
    u64 local = 0;
    for (u64 n = w->start; !g_found; n += w->stride) {
        if ((local & 0xFFFFF) == 0 && now_ms() > w->deadline) break;
        char digits[24];
        int dl = snprintf(digits, sizeof digits, "%llu", (unsigned long long)n);
        /* tail_len can be up to RATE-1 and a nonce can be up to 20 digits, so
           the message tail may span two blocks. Absorb whole blocks while the
           remainder is at least one full block, then pad what is left. */
        u8 buf[RATE * 2];
        memcpy(buf, w->tail, w->tail_len);
        memcpy(buf + w->tail_len, digits, dl);
        int len = w->tail_len + dl;

        u64 st[25];
        memcpy(st, w->base, sizeof st);

        u8 *p = buf;
        while (len >= RATE) {
            u8 blk[RATE];
            memcpy(blk, p, RATE);
            absorb_block(st, blk);
            p += RATE;
            len -= RATE;
        }
        u8 blk[RATE];
        memset(blk, 0, RATE);
        memcpy(blk, p, len);
        blk[len] ^= 0x01;
        blk[RATE - 1] ^= 0x80;
        absorb_block(st, blk);

        u8 h[32];
        for (int i = 0; i < 4; i++) {
            u64 v = st[i];
            memcpy(h + i * 8, &v, 8);
        }
        local++;
        int z = leading_zeros(h);
        if (z >= w->target) {
            g_found = 1;
            g_nonce = n;
            g_zeros = z;
            __sync_fetch_and_add(&g_hashes, local);
            return NULL;
        }
    }
    __sync_fetch_and_add(&g_hashes, local);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <prefix> <target_bits> <start_nonce> [stride] [budget_sec]\n", argv[0]);
        return 2;
    }
    const char *prefix = argv[1];
    int target = atoi(argv[2]);
    u64 start = strtoull(argv[3], NULL, 10);
    u64 stride = argc > 4 ? strtoull(argv[4], NULL, 10) : 1;
    int budget = argc > 5 ? atoi(argv[5]) : 3600;

    int plen = (int)strlen(prefix);
    int full = plen / RATE;
    int rem = plen - full * RATE;

    static work_t w;
    memset(w.base, 0, sizeof w.base);
    for (int i = 0; i < full; i++) absorb_block(w.base, (const u8 *)prefix + i * RATE);
    memset(w.tail, 0, RATE);
    memcpy(w.tail, prefix + full * RATE, rem);
    w.tail_len = rem;
    w.target = target;
    w.start = start;
    w.stride = stride;
    w.deadline = now_ms() + (u64)budget * 1000ULL;

    u64 t0 = now_ms();
    worker(&w);
    u64 dt = now_ms() - t0;

    if (g_found)
        printf("FOUND nonce=%llu zeros=%d hashes=%llu %.1fs\n",
               (unsigned long long)g_nonce, g_zeros,
               (unsigned long long)g_hashes, dt / 1000.0);
    else
        printf("NONE hashes=%llu %.1fs\n", (unsigned long long)g_hashes, dt / 1000.0);
    return g_found ? 0 : 1;
}
