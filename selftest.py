"""Smoke-test every solver in this library on synthetic data.

Run this after `./build.sh`. It proves each binary works on a known input before
you trust it against a live timed gate — the alternative is discovering a broken
build while a 600 s clock is running and the single submission is at stake.

    python3 selftest.py
"""
import json
import os
import random
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "sliding"))
import puzzle15_core as P15  # noqa: E402


def runs(line):
    out, n = [], 0
    for x in line:
        if x:
            n += 1
        elif n:
            out.append(n)
            n = 0
    if n:
        out.append(n)
    return out


def ok(msg):
    print(f"  PASS {msg}")


def fail(msg):
    print(f"  FAIL {msg}")
    return False


def test_nonogram(size=40, density=0.30, seed=7):
    """Build a board from a known grid, then require the solver to satisfy it."""
    random.seed(seed)
    g = [[1 if random.random() < density else 0 for _ in range(size)]
         for _ in range(size)]
    rows = [runs(g[r]) for r in range(size)]
    cols = [runs([g[r][c] for r in range(size)]) for c in range(size)]

    txt = ["%d %d" % (size, size)]
    for cl in rows + cols:
        txt.append(" ".join(map(str, [len(cl)] + cl)))
    stdin = "\n".join(txt) + "\n"

    t0 = time.time()
    p = subprocess.run([os.path.join(HERE, "nonogram", "nonobp"),
                        "1", "0", "300", "3", "20"],
                       input=stdin, capture_output=True, text=True, timeout=120)
    dt = time.time() - t0
    if p.returncode != 0:
        return fail(f"nonobp rc={p.returncode} after {dt:.1f}s")

    out = [[int(c) for c in l.strip()] for l in p.stdout.splitlines() if l.strip()]
    if len(out) != size:
        return fail(f"nonobp returned {len(out)} rows")
    for r in range(size):
        if runs(out[r]) != rows[r]:
            return fail(f"row {r} clue mismatch")
    for c in range(size):
        if runs([out[r][c] for r in range(size)]) != cols[c]:
            return fail(f"col {c} clue mismatch")
    ok(f"nonogram {size}x{size} solved + verified in {dt:.1f}s")
    return True


def test_sliding():
    seed = 424242
    board = P15.scramble_of(seed)
    t0 = time.time()
    moves = P15.solve(board)
    dt = time.time() - t0
    if moves is None:
        return fail("puzzle15 no solution")
    if not P15.verify(board, moves):
        return fail("puzzle15 replay mismatch")
    ok(f"sliding puzzle solved in {len(moves)} moves ({dt:.1f}s)")
    return True


def test_sha256_pow(bits=12):
    t0 = time.time()
    p = subprocess.run([os.path.join(HERE, "pow", "sha256_prefix_nonce"),
                        "deadbeef", str(bits), "2", "20"],
                       capture_output=True, text=True, timeout=40)
    dt = time.time() - t0
    if p.returncode != 0 or not p.stdout.strip():
        return fail(f"sha256 pow rc={p.returncode} out={p.stdout[:80]!r}")
    ok(f"sha256 PoW found {bits}-bit nonce in {dt:.1f}s -> {p.stdout.strip()[:60]}")
    return True


def test_keccak_pow(bits=10):
    t0 = time.time()
    p = subprocess.run([os.path.join(HERE, "pow", "keccak_prefix_nonce"),
                        "abc", str(bits), "1", "1", "20"],
                       capture_output=True, text=True, timeout=40)
    dt = time.time() - t0
    if p.returncode != 0 or "FOUND" not in p.stdout:
        return fail(f"keccak pow rc={p.returncode} out={p.stdout[:120]!r}")
    ok(f"keccak PoW found {bits}-bit nonce in {dt:.1f}s")
    return True


def test_sat(planted=True):
    """Plant a 3-SAT instance with a known assignment, then require it back."""
    random.seed(11)
    nv, ncl = 60, 240
    truth = [random.random() < 0.5 for _ in range(nv)]
    clauses = []
    for _ in range(ncl):
        vs = random.sample(range(1, nv + 1), 3)
        # make one literal true under the planted assignment
        idx = random.randrange(3)
        lits = []
        for i, v in enumerate(vs):
            want_true = (i == idx)
            sign = 1 if truth[v - 1] == want_true else -1
            lits.append(sign * v)
        clauses.append(lits)

    path = "/tmp/selftest_sat.cnf"
    with open(path, "w") as f:
        f.write("p cnf %d %d\n" % (nv, len(clauses)))
        for cl in clauses:
            f.write(" ".join(map(str, cl)) + " 0\n")

    t0 = time.time()
    p = subprocess.run([os.path.join(HERE, "sat", "wsatk"), path, "10", "1"],
                       capture_output=True, text=True, timeout=30)
    dt = time.time() - t0
    # wsatk prints the model as a bitstring in variable order (v1 first), not as
    # DIMACS literals: "0101..." with one char per variable.
    line = p.stdout.strip().splitlines()[0] if p.stdout.strip() else ""
    if not line or set(line) - set("01"):
        return fail(f"wsatk produced no bitstring model (rc={p.returncode}, "
                    f"out={p.stdout[:60]!r})")
    asg = {i + 1: ch == "1" for i, ch in enumerate(line)}
    if not all(any(asg.get(abs(l), False) == (l > 0) for l in cl) for cl in clauses):
        return fail("wsatk model does not satisfy the CNF")
    ok(f"SAT local search solved planted 3-SAT in {dt:.1f}s")
    return True


def main():
    print("solvers selftest")
    results = [
        ("nonogram", test_nonogram),
        ("sliding", test_sliding),
        ("sha256-pow", test_sha256_pow),
        ("keccak-pow", test_keccak_pow),
        ("sat", test_sat),
    ]
    bad = 0
    for name, fn in results:
        print(f"[{name}]")
        try:
            if not fn():
                bad += 1
        except Exception as e:
            bad += 1
            print(f"  FAIL {type(e).__name__}: {e}")
    print(f"\n{len(results) - bad}/{len(results)} passed")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
