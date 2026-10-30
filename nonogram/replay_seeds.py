"""Solve known boards with the exact seeds that already won on the rented box.

nonobp is deterministic per (seed, noise, maxfail, bpit, bpevery) and
single-threaded, so replaying the winning config locally costs the same wall time
the winning process took on the box. That makes a dead SSH link survivable: the
seeds are in the log, the boards are in ch.json.

Usage: replay_seeds.py challenge.json answer.json
"""
import json
import os
import subprocess
import sys
import time

BIN = "/root/scripts/solvd/solvd-solver/nonobp"

# seed, noise, maxfail, bpit, bpevery — measured on the 256-core box
WINNERS = {
    0: (28, 0.1, 300, 3, 20),
    1: (38, 0.3, 300, 3, 40),
    2: (56, 0.5, 300, 2, 20),
    3: (46, 0.3, 300, 3, 40),
}


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


def board_text(b):
    lines = ["%d %d" % (b["height"], b["width"])]
    for cl in list(b["rows"]) + list(b["cols"]):
        lines.append(" ".join(map(str, [len(cl)] + list(cl))))
    return "\n".join(lines) + "\n"


def main():
    d = json.load(open(sys.argv[1]))
    outp = sys.argv[2]
    boards = d["boards"]
    wd = "/tmp/replay_%d" % os.getpid()
    os.makedirs(wd, exist_ok=True)
    t0 = time.time()

    procs = []
    for bi, b in enumerate(boards):
        inf = "%s/in%d.txt" % (wd, bi)
        open(inf, "w").write(board_text(b))
        cfg = WINNERS.get(bi)
        if not cfg:
            print("no known seed for board", bi)
            return 1
        of = "%s/out%d.txt" % (wd, bi)
        p = subprocess.Popen([BIN] + [str(x) for x in cfg],
                             stdin=open(inf), stdout=open(of, "w"),
                             stderr=subprocess.DEVNULL)
        procs.append((bi, p, of, cfg))
        print("board %d launched cfg=%s" % (bi, cfg), flush=True)

    res = {}
    while len(res) < len(boards):
        time.sleep(0.5)
        for bi, p, of, cfg in procs:
            if bi in res:
                continue
            if p.poll() == 0:
                try:
                    g = [[int(c) for c in l.strip()] for l in open(of) if l.strip()]
                except Exception:
                    continue
                if check(boards[bi], g):
                    res[bi] = g
                    print("board", bi, "solved", round(time.time() - t0, 1), flush=True)
            elif p.poll() is not None:
                print("board", bi, "exited rc", p.returncode, flush=True)

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
    open(outp, "w").write(s)
    print("solved", round(time.time() - t0, 1), "bytes", len(s), flush=True)


if __name__ == "__main__":
    main()
