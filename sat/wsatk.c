/* Weighted local search for NONCE instances with VARIABLE clause length.

The benchmark is not 3-SAT: clause length varies (observed 3 to 6 literals), so
every clause must be read until its 0 terminator and every literal loop must use
the clause's own length. Assuming three literals per clause silently misaligns
the whole formula, which is why earlier runs stalled a handful of clauses short
of a solution: they were solving a corrupted instance.

Algorithm: probSAT-style weighted break count (cb^-break) with clause weighting
on stall, plus optional warm start.

Build: gcc -O3 -march=native -o wsatk wsatk.c -lm
Usage: ./wsatk <cnf> [seconds] [seed] [warm_file] [best_out] [cb]
Prints the assignment on stdout when every clause is satisfied.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <time.h>

static uint64_t rs;
static inline uint64_t xs(void) { uint64_t x = rs; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return rs = x; }
static inline uint32_t rb(uint32_t n) { return n ? (uint32_t)(xs() % n) : 0; }
static inline double rd(void) { return (double)(xs() >> 11) * (1.0 / 9007199254740992.0); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s cnf [seconds] [seed] [warm] [best_out] [cb]\n", argv[0]);
        return 2;
    }
    double tl = argc > 2 ? atof(argv[2]) : 30.0;
    rs = argc > 3 ? strtoull(argv[3], NULL, 10) : 88172645463325252ULL;
    if (!rs) rs = 1;
    const char *warm_path = argc > 4 ? argv[4] : NULL;
    const char *best_out = argc > 5 ? argv[5] : NULL;
    double cb = argc > 6 ? atof(argv[6]) : 2.5;

    FILE *f = fopen(argv[1], "r");
    if (!f) { perror("open cnf"); return 1; }
    char line[4096];
    int nv = 0, nc = 0;
    while (fgets(line, sizeof line, f)) if (line[0] == 'p') { sscanf(line, "p cnf %d %d", &nv, &nc); break; }
    if (nv <= 0 || nc <= 0) { fprintf(stderr, "bad header\n"); return 1; }

    /* Read the whole file's integers, skipping the header and comment lines.
       Clauses are 0-terminated and may have any number of literals, so nothing
       can assume a fixed width. */
    rewind(f);
    size_t cap = 1 << 20, ntok = 0;
    int *tok = malloc(cap * sizeof(int));
    while (fgets(line, sizeof line, f)) {
        if (line[0] == 'p' || line[0] == 'c') continue;
        char *pch = line;
        while (*pch) {
            while (*pch == ' ' || *pch == '\t' || *pch == '\n' || *pch == '\r') pch++;
            if (!*pch) break;
            char *endp = NULL;
            long v = strtol(pch, &endp, 10);
            if (endp == pch) break;
            pch = endp;
            if (ntok == cap) { cap *= 2; tok = realloc(tok, cap * sizeof(int)); }
            tok[ntok++] = (int)v;
        }
    }
    fclose(f);
    size_t start = 0;

    /* First pass: clause boundaries. */
    int *cstart = malloc(sizeof(int) * (size_t)(nc + 1));
    int *clen = malloc(sizeof(int) * (size_t)nc);
    int found = 0;
    {
        size_t i = start;
        int cur = 0;
        int begin = 0;
        while (i < ntok && found < nc) {
            int v = tok[i];
            if (v == 0) {
                cstart[found] = begin;
                clen[found] = cur;
                found++;
                begin = (int)i + 1;
                cur = 0;
            } else {
                cur++;
            }
            i++;
        }
    }
    if (found != nc) {
        fprintf(stderr, "clause count mismatch: header %d, parsed %d\n", nc, found);
        return 1;
    }
    cstart[nc] = (int)ntok;

    /* Flat literal array plus per-clause offsets. */
    int total_lits = 0;
    for (int c = 0; c < nc; c++) total_lits += clen[c];
    int *lit = malloc(sizeof(int) * (size_t)total_lits);
    int *off = malloc(sizeof(int) * (size_t)(nc + 1));
    {
        int w = 0;
        for (int c = 0; c < nc; c++) {
            off[c] = w;
            for (int k = 0; k < clen[c]; k++) lit[w++] = tok[cstart[c] + k];
        }
        off[nc] = w;
    }

    int maxocc = 0;
    for (int c = 0; c < nc; c++) if (clen[c] > maxocc) maxocc = clen[c];
    if (maxocc < 1) { fprintf(stderr, "empty clauses\n"); return 1; }

    /* occurrence lists over literals */
    int *os = calloc(2 * nv + 1, sizeof(int));
    for (int c = 0; c < nc; c++)
        for (int k = off[c]; k < off[c + 1]; k++) {
            int l = lit[k];
            int v = abs(l) - 1;
            if (v < 0 || v >= nv) { fprintf(stderr, "literal out of range: %d\n", l); return 1; }
            os[2 * v + (l < 0)]++;
        }
    int *of = malloc(sizeof(int) * (size_t)(2 * nv));
    int tot = 0;
    for (int i = 0; i < 2 * nv; i++) { of[i] = tot; tot += os[i]; os[i] = of[i]; }
    os[2 * nv] = tot;
    int *occ = malloc(sizeof(int) * (size_t)tot);
    for (int c = 0; c < nc; c++)
        for (int k = off[c]; k < off[c + 1]; k++) {
            int l = lit[k];
            int v = abs(l) - 1;
            occ[of[2 * v + (l < 0)]++] = c;
        }
    free(of);

    int *sc = calloc(nc, sizeof(int));
    int *wt = malloc(sizeof(int) * (size_t)nc);
    int *pos = malloc(sizeof(int) * (size_t)nc);
    int *uns = malloc(sizeof(int) * (size_t)nc);
    unsigned char *asg = malloc(nv);
    unsigned char *best_asg = malloc(nv);
    unsigned char *warm_asg = NULL;

    int warm = 0;
    if (warm_path) {
        FILE *w = fopen(warm_path, "r");
        if (w) {
            int ch, i = 0;
            while ((ch = fgetc(w)) != EOF && i < nv)
                if (ch == '0' || ch == '1') asg[i++] = (unsigned char)(ch == '1');
            fclose(w);
            if (i == nv) {
                warm = 1;
                warm_asg = malloc(nv);
                memcpy(warm_asg, asg, nv);
            } else {
                fprintf(stderr, "warm ignored (%d of %d)\n", i, nv);
            }
        }
    }
    if (!warm) for (int v = 0; v < nv; v++) asg[v] = (unsigned char)(xs() & 1);

    double wtab[64];
    for (int b = 0; b < 64; b++) wtab[b] = pow(cb, -(double)b);
    for (int c = 0; c < nc; c++) wt[c] = 1;

    int nu = 0;
    for (int c = 0; c < nc; c++) {
        int s = 0;
        for (int k = off[c]; k < off[c + 1]; k++) {
            int l = lit[k];
            if ((l > 0) == (asg[abs(l) - 1] == 1)) s++;
        }
        sc[c] = s;
        if (!s) { pos[c] = nu; uns[nu++] = c; }
    }
    int best = nu;
    memcpy(best_asg, asg, nv);

    long fl = 0, last_gain = 0;
    double t0 = now();
    int *brk = malloc(sizeof(int) * (size_t)maxocc);
    double *wbuf = malloc(sizeof(double) * (size_t)maxocc);

    while (nu > 0) {
        if ((fl & 0x3FF) == 0 && now() - t0 > tl) break;
        int c = uns[rb((uint32_t)nu)];
        int k = clen[c];
        double ws = 0;
        for (int j = 0; j < k; j++) {
            int l = lit[off[c] + j];
            int vv = abs(l) - 1;
            int b = 0;
            int slot = 2 * vv + (asg[vv] ? 0 : 1);
            for (int q = os[slot]; q < os[slot + 1]; q++) if (sc[occ[q]] == 1) b += wt[occ[q]];
            brk[j] = b;
            wbuf[j] = wtab[b < 63 ? b : 62];
            ws += wbuf[j];
        }
        int pick = 0;
        double r = rd() * ws, acc = 0;
        for (int j = 0; j < k; j++) { acc += wbuf[j]; if (r <= acc) { pick = j; break; } pick = j; }
        int vv = abs(lit[off[c] + pick]) - 1;

        int was = asg[vv];
        asg[vv] = !was;
        int keep = 2 * vv + (was ? 0 : 1), gain = 2 * vv + (was ? 1 : 0);
        for (int q = os[keep]; q < os[keep + 1]; q++) {
            int ci = occ[q];
            if (--sc[ci] == 0) { pos[ci] = nu; uns[nu++] = ci; }
        }
        for (int q = os[gain]; q < os[gain + 1]; q++) {
            int ci = occ[q];
            if (sc[ci]++ == 0) { int p = pos[ci]; uns[p] = uns[--nu]; pos[uns[p]] = p; }
        }
        fl++;
        if (nu < best) { best = nu; memcpy(best_asg, asg, nv); last_gain = fl; if (best == 0) break;
            // Dump on every improvement. The farm harness kills this process at
            // its budget, and a dump written only on exit is lost when the kill
            // lands first (the shard then uploads nothing at all). Improvements
            // become rare quickly, so the extra writes are negligible.
            if (best_out) {
                FILE *o = fopen(best_out, "w");
                if (o) { for (int v2 = 0; v2 < nv; v2++) fputc(best_asg[v2] ? '1' : '0', o); fputc('\n', o); fclose(o); }
            }
        }

        if (fl - last_gain > 50000L) {
            for (int j = 0; j < nu; j++) { int ci = uns[j]; if (wt[ci] < 2000) wt[ci]++; }
            last_gain = fl;
        }
        if ((fl & 0x7FFFFF) == 0 && best > 4) {
            // Restart from the BEST assignment found, not from the warm start.
            // Restoring the warm start discarded the best solution on every
            // restart, so a run that reached 35 unsatisfied clauses jumped back
            // to the 505-unsatisfied spectral guess and had to re-earn it.
            memcpy(asg, best_asg, nv);
            // Small perturbation: when the best is already near a solution a
            // 10% kick destroys it, so kick 1% (at least 4 variables).
            int kick = nv / 100 + 4;
            for (int q = 0; q < kick; q++) asg[rb((uint32_t)nv)] ^= 1;
            nu = 0;
            for (int ci = 0; ci < nc; ci++) {
                int s = 0;
                for (int q2 = off[ci]; q2 < off[ci + 1]; q2++) {
                    int l = lit[q2];
                    if ((l > 0) == (asg[abs(l) - 1] == 1)) s++;
                }
                sc[ci] = s;
                if (!s) { pos[ci] = nu; uns[nu++] = ci; }
            }
            // Only accept the post-kick assignment if it actually beats the
            // record. Assigning unconditionally here threw the record away on
            // every restart: a run started from a 36-unsatisfied warm start and
            // reported 132 at the end because the last kicked copy replaced it.
            if (nu < best) { best = nu; memcpy(best_asg, asg, nv); }
        }
    }

    if (nu == 0) {
        fprintf(stderr, "SAT flips=%ld t=%.2fs\n", fl, now() - t0);
        for (int v = 0; v < nv; v++) putchar(asg[v] ? '1' : '0');
        putchar('\n');
        return 0;
    }
    fprintf(stderr, "UNKNOWN flips=%ld best_unsat=%d t=%.1fs\n", fl, best, now() - t0);
    if (best_out) {
        FILE *o = fopen(best_out, "w");
        if (o) { for (int v = 0; v < nv; v++) fputc(best_asg[v] ? '1' : '0', o); fputc('\n', o); fclose(o); }
    }
    return 3;
}
