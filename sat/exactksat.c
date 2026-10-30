// exactksat: local search for a planted k-SAT whose clauses each hold a FIXED
// number of true literals under the hidden assignment.
//
// Why: on NONCE's balanced-1536 benchmark the sign statistics carry no signal
// (same-sign literal pair rate 0.497, positive fraction 0.500), so spectral and
// break-count local search both see noise and stall ~990 clauses short. If the
// generator plants with a constant true-literal count T, a different objective
// exposes it: minimise sum |nTrue(c) - T| instead of the unsatisfied count. Run
// several T in parallel and the objective itself says which one is right.
//
// Usage: exactksat <cnf> <T> <seed> <seconds>
// Prints "EXACT <bits>" and exits 0 on success.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int nv, nc;
static int *cl_start, *cl_len, *cl_lit;
static unsigned char *asg;
static int *tcnt, *bad, *badpos, nbad;
static int *occ_start, *occ_list;
static unsigned long long rs;

static inline unsigned long long xr(void) {
    rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs;
}
static inline int lit_true(int l) {
    int v = l < 0 ? -l : l;
    return l > 0 ? asg[v] : !asg[v];
}
static inline int lit_slot(int l) {
    int v = l < 0 ? -l : l;
    return (v - 1) * 2 + (l < 0 ? 1 : 0);
}
static inline void bad_add(int c) { badpos[c] = nbad; bad[nbad++] = c; }
static inline void bad_del(int c) {
    int p = badpos[c], last = bad[--nbad];
    bad[p] = last; badpos[last] = p; badpos[c] = -1;
}
static inline void bad_fix(int c, int T) {
    int is_bad = tcnt[c] != T;
    if (is_bad && badpos[c] < 0) bad_add(c);
    else if (!is_bad && badpos[c] >= 0) bad_del(c);
}

// Flip variable v and maintain every affected clause's true count.
static void flip_var(int v, int T) {
    int tslot = (v - 1) * 2 + (asg[v] ? 0 : 1);  // literal currently TRUE
    int fslot = (v - 1) * 2 + (asg[v] ? 1 : 0);  // literal currently FALSE
    for (int i = occ_start[tslot]; i < occ_start[tslot + 1]; i++) {
        int c = occ_list[i];
        tcnt[c]--;
        bad_fix(c, T);
    }
    for (int i = occ_start[fslot]; i < occ_start[fslot + 1]; i++) {
        int c = occ_list[i];
        tcnt[c]++;
        bad_fix(c, T);
    }
    asg[v] ^= 1;
}

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: %s cnf T seed seconds\n", argv[0]); return 2; }
    int T = atoi(argv[2]);
    rs = strtoull(argv[3], 0, 10) | 1ULL;
    double tl = atof(argv[4]);

    FILE *f = fopen(argv[1], "r");
    if (!f) { perror("open"); return 2; }
    char line[1024];
    int cap = 4096, nl = 0, ccap = 1024;
    cl_lit = malloc(sizeof(int) * cap);
    cl_start = malloc(sizeof(int) * ccap);
    cl_len = malloc(sizeof(int) * ccap);
    nc = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == 'c' || line[0] == 'p' || line[0] == '\n') continue;
        if (nc >= ccap) {
            ccap *= 2;
            cl_start = realloc(cl_start, sizeof(int) * ccap);
            cl_len = realloc(cl_len, sizeof(int) * ccap);
        }
        cl_start[nc] = nl;
        int len = 0;
        char *p = line;
        for (;;) {
            while (*p == ' ') p++;
            if (!*p || *p == '\n') break;
            long x = strtol(p, &p, 10);
            if (x == 0) break;
            if (nl >= cap) { cap *= 2; cl_lit = realloc(cl_lit, sizeof(int) * cap); }
            cl_lit[nl++] = (int)x;
            len++;
        }
        cl_len[nc++] = len;
    }
    fclose(f);
    for (int i = 0; i < nl; i++) { int v = abs(cl_lit[i]); if (v > nv) nv = v; }

    // occurrence lists over 2*nv literal slots
    int *cnt = calloc(2 * nv, sizeof(int));
    for (int i = 0; i < nl; i++) cnt[lit_slot(cl_lit[i])]++;
    occ_start = malloc(sizeof(int) * (2 * nv + 1));
    int s = 0;
    for (int i = 0; i < 2 * nv; i++) { occ_start[i] = s; s += cnt[i]; }
    occ_start[2 * nv] = s;
    occ_list = malloc(sizeof(int) * (s > 0 ? s : 1));
    int *fill = malloc(sizeof(int) * 2 * nv);
    memcpy(fill, occ_start, sizeof(int) * 2 * nv);
    for (int c = 0; c < nc; c++)
        for (int j = cl_start[c]; j < cl_start[c] + cl_len[c]; j++)
            occ_list[fill[lit_slot(cl_lit[j])]++] = c;
    free(fill); free(cnt);

    asg = malloc(nv + 1);
    tcnt = malloc(sizeof(int) * nc);
    bad = malloc(sizeof(int) * nc);
    badpos = malloc(sizeof(int) * nc);

    struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0);
    long long flips = 0;
    for (;;) {
        for (int v = 1; v <= nv; v++) asg[v] = (unsigned char)((xr() >> 33) & 1);
        nbad = 0;
        for (int c = 0; c < nc; c++) {
            int t = 0;
            for (int j = cl_start[c]; j < cl_start[c] + cl_len[c]; j++)
                if (lit_true(cl_lit[j])) t++;
            tcnt[c] = t; badpos[c] = -1;
            if (t != T) bad_add(c);
        }
        while (nbad > 0) {
            int c = bad[(int)(xr() % (unsigned)nbad)];
            int base = cl_start[c], len = cl_len[c], t = tcnt[c];
            int want_true_lit = t < T;   // too few true: raise one; too many: drop one
            int cand[64], ncand = 0;
            for (int j = 0; j < len; j++) {
                int l = cl_lit[base + j];
                if (lit_true(l) == want_true_lit) cand[ncand++] = l;
            }
            if (ncand) {
                int l = cand[(int)(xr() % (unsigned)ncand)];
                flip_var(abs(l), T);
            }
            flips++;
            if ((flips & 0x3FF) == 0) {
                struct timespec tn; clock_gettime(CLOCK_MONOTONIC, &tn);
                double el = (tn.tv_sec - t0.tv_sec) + (tn.tv_nsec - t0.tv_nsec) / 1e9;
                if (el > tl) {
                    fprintf(stderr, "T=%d TIMEOUT flips=%lld bad=%d\n", T, flips, nbad);
                    return 1;
                }
            }
        }
        printf("EXACT ");
        for (int v = 1; v <= nv; v++) putchar(asg[v] ? '1' : '0');
        putchar('\n');
        fprintf(stderr, "T=%d SOLVED flips=%lld\n", T, flips);
        return 0;
    }
}
