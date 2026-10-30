"""Wide-seed portfolio runner: one unique seed per process.

psolve.py cycles 12 fixed configs across every slot, so with 64 processes per
board 52 of them redo identical work (nonobp is deterministic per seed). This
runner gives every process its own seed, which is what actually uses a big box.

Usage: psolve_wide.py challenge.json answer.json [procs_per_board]
"""
import json
import os
import subprocess
import sys
import time

BIN = os.environ.get("NONOBP", os.path.join(os.path.dirname(os.path.abspath(__file__)), "nonobp"))


def runs(line):
    out, n = [], 0
    for x in line:
        if x == 1:
            n += 1
        elif n:
            out.append(n)
            n = 0
    if n:
        out.append(n)
    return out


def check(b, g):
    H, W = b["height"], b["width"]
    return (len(g) == H and all(len(r) == W for r in g)
            and all(runs(g[r]) == list(b["rows"][r]) for r in range(H))
            and all(runs([g[r][c] for r in range(H)]) == list(b["cols"][c])
                    for c in range(W)))


def configs(n):
    """Unique (seed, noise, maxfail, bpit, bpevery) per slot."""
    out = []
    noises = [0.0, 0.0, 0.0, 0.1, 0.2, 0.3, 0.4, 0.5]
    bpits = [3, 2, 1, 3, 2, 3, 1, 2]
    bpevery = [20, 10, 1, 20, 10, 40, 5, 20]
    maxfails = [300, 300, 500, 300, 200, 300, 1000, 300]
    for i in range(n):
        out.append((i + 1, noises[i % len(noises)], maxfails[i % len(maxfails)],
                    bpits[i % len(bpits)], bpevery[i % len(bpevery)]))
    return out


def main():
    d = json.load(open(sys.argv[1]))
    outp = sys.argv[2]
    per = int(sys.argv[3]) if len(sys.argv) > 3 else 32
    t = time.time()
    boards = d["boards"]
    nb = len(boards)
    ncpu = os.cpu_count() or 4
    per = max(1, min(per, ncpu // nb))

    wd = "/tmp/wpsolve_%d" % os.getpid()
    os.makedirs(wd, exist_ok=True)

    procs = {}
    for bi, b in enumerate(boards):
        inf = "%s/in%d.txt" % (wd, bi)
        with open(inf, "w") as f:
            f.write("%d %d\n" % (b["height"], b["width"]))
            for cl in list(b["rows"]) + list(b["cols"]):
                f.write(" ".join(map(str, [len(cl)] + list(cl))) + "\n")
        procs[bi] = []
        for cfg in configs(per):
            of = "%s/out%d_%d.txt" % (wd, bi, cfg[0])
            p = subprocess.Popen([BIN] + [str(x) for x in cfg],
                                 stdin=open(inf), stdout=open(of, "w"),
                                 stderr=subprocess.DEVNULL)
            procs[bi].append((p, of, cfg))

    print("launched %d procs/board x %d boards" % (per, nb), flush=True)
    res = {}
    deadline = d["expiresAt"] - 20
    while len(res) < nb:
        if time.time() > deadline:
            break
        time.sleep(0.2)
        for bi in range(nb):
            if bi in res:
                continue
            for p, of, cfg in procs[bi]:
                if p.poll() == 0:
                    try:
                        g = [[int(c) for c in l.strip()] for l in open(of) if l.strip()]
                    except Exception:
                        continue
                    if check(boards[bi], g):
                        res[bi] = g
                        print("board", bi, "solved by", cfg,
                              round(time.time() - t, 1), flush=True)
                        for q, _, _ in procs[bi]:
                            if q.poll() is None:
                                q.kill()
                        break
            if bi not in res and all(p.poll() is not None for p, _, _ in procs[bi]):
                print("board", bi, "all failed", flush=True)
                break

    for bi in procs:
        for q, _, _ in procs[bi]:
            if q.poll() is None:
                q.kill()

    if len(res) < nb:
        print("FAIL", len(res), "/", nb, flush=True)
        sys.exit(1)

    ans = []
    for bi, b in enumerate(boards):
        H, W = b["height"], b["width"]
        g = res[bi]
        rows = []
        for r in range(H):
            bits = "".join(map(str, g[r]))
            nh = (W + 3) // 4
            bits += "0" * (nh * 4 - W)
            rows.append("".join("%x" % int(bits[i:i + 4], 2)
                                for i in range(0, len(bits), 4)))
        dec = [[int(c) for c in "".join(format(int(ch, 16), "04b") for ch in h)[:W]]
               for h in rows]
        assert len(rows) == H and check(b, dec), ("verify fail", bi)
        ans.append(rows)

    s = json.dumps({"challengeId": d["challengeId"], "encoding": "hex",
                    "boards": ans}, separators=(",", ":"))
    assert len(s) <= d["submit"]["maxBodyBytes"], "too big"
    open(outp, "w").write(s)
    print("solved", round(time.time() - t, 1), "bytes", len(s), flush=True)


if __name__ == "__main__":
    main()
