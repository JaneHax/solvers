/* Zerolings PoW miner: sha256(prefix ++ nonce_be32) with N leading zero bits. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    uint32_t h[8];
    uint64_t len;
    unsigned char buf[64];
    size_t buflen;
} sha256_ctx;

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void block(sha256_ctx *c, const unsigned char *p) {
    uint32_t w[64], a, b, d, e, f, g, h, cc, t1, t2;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (i = 0; i < 64; i++) {
        uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

static void sha_init(sha256_ctx *c) {
    static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(c->h, iv, sizeof(iv));
    c->len = 0;
    c->buflen = 0;
}

static void sha_update(sha256_ctx *c, const void *data, size_t n) {
    const unsigned char *p = data;
    c->len += n;
    while (n) {
        size_t take = 64 - c->buflen;
        if (take > n) take = n;
        memcpy(c->buf + c->buflen, p, take);
        c->buflen += take; p += take; n -= take;
        if (c->buflen == 64) { block(c, c->buf); c->buflen = 0; }
    }
}

static void sha_final(sha256_ctx *c, unsigned char out[32]) {
    uint64_t bits = c->len * 8;
    unsigned char pad = 0x80;
    sha_update(c, &pad, 1);
    unsigned char z = 0;
    while (c->buflen != 56) sha_update(c, &z, 1);
    unsigned char lenb[8];
    for (int i = 0; i < 8; i++) lenb[i] = (unsigned char)(bits >> (56 - i * 8));
    sha_update(c, lenb, 8);
    for (int i = 0; i < 8; i++) {
        out[i * 4] = (unsigned char)(c->h[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(c->h[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(c->h[i] >> 8);
        out[i * 4 + 3] = (unsigned char)c->h[i];
    }
}

static int leading_zeros(const unsigned char h[32]) {
    int z = 0;
    for (int i = 0; i < 32; i++) {
        if (h[i] == 0) { z += 8; continue; }
        for (int b = 7; b >= 0; b--) {
            if (h[i] & (1u << b)) return z;
            z++;
        }
        break;
    }
    return z;
}

typedef struct {
    unsigned char prefix[128];
    size_t plen;
    int target;
    unsigned long long start;
    unsigned long long step;
    volatile int *found;
    unsigned long long nonce;
    int zeros;
    unsigned char hash[32];
} work_t;

static volatile unsigned long long g_hashes = 0;
static volatile int g_stop = 0;

static void *worker(void *arg) {
    work_t *w = arg;
    unsigned char buf[256];
    memcpy(buf, w->prefix, w->plen);
    unsigned char h[32];
    unsigned long long local = 0;
    for (unsigned long long n = w->start; !g_stop; n += w->step) {
        /* uint256 nonce, big-endian: 24 zero bytes then the 8-byte value.
           Shifting by a negative amount (the naive i*8 loop) is UB and produced
           hashes that never matched the contract. */
        memset(buf + w->plen, 0, 32);
        for (int i = 0; i < 8; i++)
            buf[w->plen + 24 + i] = (unsigned char)(n >> (56 - i * 8));
        sha256_ctx c;
        sha_init(&c);
        sha_update(&c, buf, w->plen + 32);
        sha_final(&c, h);
        local++;
        int z = leading_zeros(h);
        if (z >= w->target) {
            w->nonce = n;
            w->zeros = z;
            memcpy(w->hash, h, 32);
            g_stop = 1;
            *w->found = 1;
            break;
        }
    }
    g_hashes += local;
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <prefix_hex> <target_bits> [threads] [budget_s]\n", argv[0]);
        return 2;
    }
    work_t w;
    memset(&w, 0, sizeof(w));
    const char *hex = argv[1];
    if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex += 2;
    w.plen = strlen(hex) / 2;
    for (size_t i = 0; i < w.plen; i++) {
        unsigned int v;
        sscanf(hex + i * 2, "%2x", &v);
        w.prefix[i] = (unsigned char)v;
    }
    w.target = atoi(argv[2]);
    int threads = argc > 3 ? atoi(argv[3]) : 4;
    int budget = argc > 4 ? atoi(argv[4]) : 0;
    static int found = 0;
    w.found = &found;
    w.step = threads;

    pthread_t th[64];
    work_t jobs[64];
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < threads; i++) {
        /* Each thread gets its own work_t: sharing one struct and mutating
           w.start before pthread_create made every thread search the same
           sequence. */
        jobs[i] = w;
        jobs[i].start = i;
        jobs[i].step = threads;
        pthread_create(&th[i], NULL, worker, &jobs[i]);
    }
    while (!found) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (budget && now.tv_sec - t0.tv_sec >= budget) { g_stop = 1; break; }
        struct timespec sl = {0, 100 * 1000 * 1000};
        nanosleep(&sl, NULL);
    }
    for (int i = 0; i < threads; i++) pthread_join(th[i], NULL);
    for (int i = 0; i < threads; i++) {
        if (jobs[i].zeros >= w.target) {
            w.nonce = jobs[i].nonce;
            w.zeros = jobs[i].zeros;
            memcpy(w.hash, jobs[i].hash, 32);
            break;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double dt = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    if (found) {
        printf("FOUND nonce=%llu zeros=%d hashes=%llu %.1fs\n", w.nonce, w.zeros, g_hashes, dt);
        printf("hash=0x");
        for (int i = 0; i < 32; i++) printf("%02x", w.hash[i]);
        printf("\n");
    } else {
        printf("NONE hashes=%llu %.1fs\n", g_hashes, dt);
    }
    return found ? 0 : 1;
}
