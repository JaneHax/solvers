/* Keccak-256 PoW miner for HashOre: keccak256(challenge ++ nonce_be32). */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef uint64_t u64;
typedef unsigned char u8;

static const u64 RC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL};

static const int ROTC[24] = {1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
                             27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44};
static const int PILN[24] = {10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
                             15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1};

/* Fully unrolled keccak-f[1600]. The array version with computed indices ran at
   ~640K hashes/s on this box; keeping the 25 lanes in registers gets roughly 3x
   that, which is the difference between 2 hours and 40 minutes for 31-bit work. */
#define ROL(x, n) (((x) << (n)) | ((x) >> (64 - (n))))

static void keccakf(u64 st[25]) {
    u64 a00 = st[0], a01 = st[1], a02 = st[2], a03 = st[3], a04 = st[4];
    u64 a05 = st[5], a06 = st[6], a07 = st[7], a08 = st[8], a09 = st[9];
    u64 a10 = st[10], a11 = st[11], a12 = st[12], a13 = st[13], a14 = st[14];
    u64 a15 = st[15], a16 = st[16], a17 = st[17], a18 = st[18], a19 = st[19];
    u64 a20 = st[20], a21 = st[21], a22 = st[22], a23 = st[23], a24 = st[24];
    u64 c0, c1, c2, c3, c4, d0, d1, d2, d3, d4;
    u64 b00, b01, b02, b03, b04, b05, b06, b07, b08, b09, b10, b11, b12;
    u64 b13, b14, b15, b16, b17, b18, b19, b20, b21, b22, b23, b24;

    for (int r = 0; r < 24; r++) {
        c0 = a00 ^ a05 ^ a10 ^ a15 ^ a20;
        c1 = a01 ^ a06 ^ a11 ^ a16 ^ a21;
        c2 = a02 ^ a07 ^ a12 ^ a17 ^ a22;
        c3 = a03 ^ a08 ^ a13 ^ a18 ^ a23;
        c4 = a04 ^ a09 ^ a14 ^ a19 ^ a24;
        d0 = c4 ^ ROL(c1, 1);
        d1 = c0 ^ ROL(c2, 1);
        d2 = c1 ^ ROL(c3, 1);
        d3 = c2 ^ ROL(c4, 1);
        d4 = c3 ^ ROL(c0, 1);
        a00 ^= d0; a01 ^= d1; a02 ^= d2; a03 ^= d3; a04 ^= d4;
        a05 ^= d0; a06 ^= d1; a07 ^= d2; a08 ^= d3; a09 ^= d4;
        a10 ^= d0; a11 ^= d1; a12 ^= d2; a13 ^= d3; a14 ^= d4;
        a15 ^= d0; a16 ^= d1; a17 ^= d2; a18 ^= d3; a19 ^= d4;
        a20 ^= d0; a21 ^= d1; a22 ^= d2; a23 ^= d3; a24 ^= d4;

        b00 = a00;
        b01 = ROL(a06, 44);
        b02 = ROL(a12, 43);
        b03 = ROL(a18, 21);
        b04 = ROL(a24, 14);
        b05 = ROL(a03, 28);
        b06 = ROL(a09, 20);
        b07 = ROL(a10, 3);
        b08 = ROL(a16, 45);
        b09 = ROL(a22, 61);
        b10 = ROL(a01, 1);
        b11 = ROL(a07, 6);
        b12 = ROL(a13, 25);
        b13 = ROL(a19, 8);
        b14 = ROL(a20, 18);
        b15 = ROL(a04, 27);
        b16 = ROL(a05, 36);
        b17 = ROL(a11, 10);
        b18 = ROL(a17, 15);
        b19 = ROL(a23, 56);
        b20 = ROL(a02, 62);
        b21 = ROL(a08, 55);
        b22 = ROL(a14, 39);
        b23 = ROL(a15, 41);
        b24 = ROL(a21, 2);

        a00 = b00 ^ ((~b01) & b02);
        a01 = b01 ^ ((~b02) & b03);
        a02 = b02 ^ ((~b03) & b04);
        a03 = b03 ^ ((~b04) & b00);
        a04 = b04 ^ ((~b00) & b01);
        a05 = b05 ^ ((~b06) & b07);
        a06 = b06 ^ ((~b07) & b08);
        a07 = b07 ^ ((~b08) & b09);
        a08 = b08 ^ ((~b09) & b05);
        a09 = b09 ^ ((~b05) & b06);
        a10 = b10 ^ ((~b11) & b12);
        a11 = b11 ^ ((~b12) & b13);
        a12 = b12 ^ ((~b13) & b14);
        a13 = b13 ^ ((~b14) & b10);
        a14 = b14 ^ ((~b10) & b11);
        a15 = b15 ^ ((~b16) & b17);
        a16 = b16 ^ ((~b17) & b18);
        a17 = b17 ^ ((~b18) & b19);
        a18 = b18 ^ ((~b19) & b15);
        a19 = b19 ^ ((~b15) & b16);
        a20 = b20 ^ ((~b21) & b22);
        a21 = b21 ^ ((~b22) & b23);
        a22 = b22 ^ ((~b23) & b24);
        a23 = b23 ^ ((~b24) & b20);
        a24 = b24 ^ ((~b20) & b21);

        a00 ^= RC[r];
    }

    st[0] = a00; st[1] = a01; st[2] = a02; st[3] = a03; st[4] = a04;
    st[5] = a05; st[6] = a06; st[7] = a07; st[8] = a08; st[9] = a09;
    st[10] = a10; st[11] = a11; st[12] = a12; st[13] = a13; st[14] = a14;
    st[15] = a15; st[16] = a16; st[17] = a17; st[18] = a18; st[19] = a19;
    st[20] = a20; st[21] = a21; st[22] = a22; st[23] = a23; st[24] = a24;
}

/* Keccak-256 (original padding 0x01), 136-byte rate. Input here is 64 bytes
   (32-byte challenge + 32-byte nonce), so it is a single absorbed block. */
static void keccak256_64(const u8 in[64], u8 out[32]) {
    u64 st[25];
    u8 blk[136];
    memset(st, 0, sizeof(st));
    memset(blk, 0, sizeof(blk));
    memcpy(blk, in, 64);
    blk[64] = 0x01;
    blk[135] |= 0x80;
    for (int i = 0; i < 17; i++) {
        u64 v = 0;
        for (int b = 0; b < 8; b++) v |= (u64)blk[i * 8 + b] << (8 * b);
        st[i] ^= v;
    }
    keccakf(st);
    for (int i = 0; i < 4; i++)
        for (int b = 0; b < 8; b++) out[i * 8 + b] = (u8)(st[i] >> (8 * b));
}

static int leading_zeros(const u8 h[32]) {
    int z = 0;
    for (int i = 0; i < 32; i++) {
        if (h[i] == 0) { z += 8; continue; }
        for (int b = 7; b >= 0; b--) {
            if (h[i] & (1u << b)) return z;
            z++;
        }
    }
    return z;
}

typedef struct {
    u8 prefix[32];
    int target;
    u64 start, step;
    u64 nonce;
    int zeros;
    u8 hash[32];
} work_t;

static volatile int g_stop = 0;
static volatile u64 g_hashes = 0;

static void *worker(void *arg) {
    work_t *w = (work_t *)arg;
    /* Pre-absorb the 136-byte block (challenge + nonce word + padding) into a
       lane template once. The nonce occupies bytes 56..63, i.e. lane 7 in
       little-endian, so the hot loop only sets one lane and runs keccakf --
       re-absorbing 136 bytes per attempt was the whole cost. */
    u8 blk[136];
    memset(blk, 0, sizeof(blk));
    memcpy(blk, w->prefix, 32);
    blk[64] = 0x01;
    blk[135] = 0x80;
    u64 tmpl[25];
    memset(tmpl, 0, sizeof(tmpl));
    for (int i = 0; i < 17; i++) {
        u64 v = 0;
        for (int b = 0; b < 8; b++) v |= (u64)blk[i * 8 + b] << (8 * b);
        tmpl[i] = v;
    }
    u64 st[25];
    u64 local = 0;
    for (u64 n = w->start; !g_stop; n += w->step) {
        memcpy(st, tmpl, sizeof(tmpl));
        st[7] = __builtin_bswap64(n);   /* BE bytes 56..63 read as a LE lane */
        keccakf(st);
        /* Hash bytes come out little-endian from the lanes, so the number of
           leading zero BITS is the position of the lowest set bit: ctz over
           lanes 0..3. Building the 32-byte digest first and scanning it was the
           hot-path cost; this rejects in one instruction. */
        int z;
        if (st[0]) z = __builtin_ctzll(st[0]);
        else if (st[1]) z = 64 + __builtin_ctzll(st[1]);
        else if (st[2]) z = 128 + __builtin_ctzll(st[2]);
        else if (st[3]) z = 192 + __builtin_ctzll(st[3]);
        else z = 256;
        local++;
        if (z >= w->target) {
            w->nonce = n;
            w->zeros = z;
            for (int i = 0; i < 4; i++)
                for (int b = 0; b < 8; b++) w->hash[i * 8 + b] = (u8)(st[i] >> (8 * b));
            g_stop = 1;
            break;
        }
    }
    __sync_fetch_and_add(&g_hashes, local);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <challenge_hex32> <target_bits> [threads] [budget_s]\n", argv[0]);
        return 2;
    }
    const char *hex = argv[1];
    if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex += 2;
    if (strlen(hex) != 64) { fprintf(stderr, "challenge must be 32 bytes\n"); return 2; }

    work_t w;
    memset(&w, 0, sizeof(w));
    for (int i = 0; i < 32; i++) { unsigned int v; sscanf(hex + i * 2, "%2x", &v); w.prefix[i] = (u8)v; }
    w.target = atoi(argv[2]);
    int threads = argc > 3 ? atoi(argv[3]) : 2;
    int budget = argc > 4 ? atoi(argv[4]) : 0;
    /* Optional explicit offset/stride so two separate processes can cover
       disjoint nonce ranges -- in-process threads did not scale on this box. */
    unsigned long long off = argc > 5 ? strtoull(argv[5], NULL, 10) : 0;
    unsigned long long stride = argc > 6 ? strtoull(argv[6], NULL, 10) : (unsigned long long)threads;

    work_t jobs[64];
    pthread_t th[64];
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < threads; i++) {
        jobs[i] = w;
        jobs[i].start = off + (unsigned long long)i;
        jobs[i].step = stride;
        pthread_create(&th[i], NULL, worker, &jobs[i]);
    }
    while (!g_stop) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (budget && now.tv_sec - t0.tv_sec >= budget) { g_stop = 1; break; }
        struct timespec sl = {0, 100 * 1000 * 1000};
        nanosleep(&sl, NULL);
    }
    for (int i = 0; i < threads; i++) pthread_join(th[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double dt = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;

    for (int i = 0; i < threads; i++) {
        if (jobs[i].zeros >= w.target) {
            printf("FOUND nonce=%llu zeros=%d hashes=%llu %.1fs\n",
                   (unsigned long long)jobs[i].nonce, jobs[i].zeros,
                   (unsigned long long)g_hashes, dt);
            printf("hash=0x");
            for (int k = 0; k < 32; k++) printf("%02x", jobs[i].hash[k]);
            printf("\n");
            return 0;
        }
    }
    printf("NONE hashes=%llu %.1fs\n", (unsigned long long)g_hashes, dt);
    return 1;
}
