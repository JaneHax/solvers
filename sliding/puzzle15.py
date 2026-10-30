"""Solve KA Seals slide puzzles (4x4 15-puzzle) and submit the move list.

The scramble is deterministic and reproducible off-chain: `scrambleOf(seed)`
starts from the solved board and makes 80 slides, each chosen by the same
32-bit hash the contract uses. So the board can be rebuilt locally from the
on-chain `seed`, solved with IDA* + linear-conflict heuristic, and the move list
posted to `solve(uint256 id, bytes moves)`.

Moves are single bytes: the board position tapped each time (0..15, row-major).
"""
import json
import sys

sys.path.insert(0, "/root/scripts/nonce-mint")
import nonce_status as ns  # noqa: E402

SEALS = "0xA9eCBD052941C9E310e68525Aa62671729B84446"
M32 = 0xFFFFFFFF


def sh(n):
    """32-bit integer hash, identical to the one in the contract."""
    x = n & M32
    x = (x ^ (x >> 15)) & M32
    x = (x * 2246822507) & M32
    x = (x ^ (x >> 13)) & M32
    x = (x * 3266489909) & M32
    return (x ^ (x >> 16)) & M32


def nbrs(g):
    r, c = g // 4, g % 4
    out = []
    if r > 0:
        out.append(g - 4)
    if r < 3:
        out.append(g + 4)
    if c > 0:
        out.append(g - 1)
    if c < 3:
        out.append(g + 1)
    return out


def scramble_of(seed):
    """80 deterministic slides from the solved board; always solvable."""
    board = list(range(16))     # board[pos] = tile
    gap = 15
    last = -1
    for n in range(80):
        nb = [x for x in nbrs(gap) if x != last]
        m = nb[sh(seed * 977 + (100 + n) * 31) % len(nb)]
        board[gap] = board[m]
        board[m] = 15
        last = gap
        gap = m
    return board


def manhattan(board):
    s = 0
    for p, v in enumerate(board):
        if v == 15:
            continue
        s += abs(v // 4 - p // 4) + abs(v % 4 - p % 4)
    return s


def linear_conflict(board):
    """Extra moves forced by tiles that are in their goal line but reversed."""
    extra = 0
    for r in range(4):
        row = [board[r * 4 + c] for c in range(4)]
        for i in range(4):
            for j in range(i + 1, 4):
                a, b = row[i], row[j]
                if a == 15 or b == 15:
                    continue
                if a // 4 == r and b // 4 == r and a > b:
                    extra += 2
    for c in range(4):
        col = [board[r * 4 + c] for r in range(4)]
        for i in range(4):
            for j in range(i + 1, 4):
                a, b = col[i], col[j]
                if a == 15 or b == 15:
                    continue
                if a % 4 == c and b % 4 == c and a > b:
                    extra += 2
    return extra


def solve_puzzle(board, budget=4000000):
    """IDA* with manhattan + linear conflicts; returns the move list or None."""
    start = board[:]
    if start == list(range(16)):
        return []
    gap = start.index(15)
    nodes = [0]

    def search(tiles, g, bound, prev, gap, man, path):
        f = g + man + linear_conflict(tiles)
        if f > bound:
            return f
        if man == 0:
            return -1
        nodes[0] += 1
        if nodes[0] > budget:
            return float("inf")
        best = float("inf")
        for m in nbrs(gap):
            if m == prev:
                continue
            v = tiles[m]
            d = (abs(v // 4 - gap // 4) + abs(v % 4 - gap % 4)) - \
                (abs(v // 4 - m // 4) + abs(v % 4 - m % 4))
            og = gap
            tiles[og] = v
            tiles[m] = 15
            path.append(m)
            r = search(tiles, g + 1, bound, og, m, man + d, path)
            if r == -1:
                return -1
            path.pop()
            tiles[m] = v
            tiles[og] = 15
            if r < best:
                best = r
            if nodes[0] > budget:
                return float("inf")
        return best

    for w in (1, 1.5, 3, 6):
        bound = manhattan(start) + linear_conflict(start)
        nodes[0] = 0
        tiles = start[:]
        path = []
        while True:
            t = search(tiles, 0, bound, -1, gap, manhattan(tiles), path)
            if t == -1:
                return path
            if t == float("inf"):
                break
            bound = t
    return None


def get_seal(tid):
    sel = "0xc1e52843"  # sealOf(uint256)
    r = ns.rpc("eth_call", [{"to": SEALS,
                             "data": sel + format(tid, "064x")}, "latest"], tries=4)
    h = r[2:]
    return {
        "id": tid,
        "seed": int(h[0:64], 16),
        "fam": int(h[64:128], 16),
        "best": int(h[128:192], 16),
        "solver": "0x" + h[192 + 24:256],
        "owner": "0x" + h[256 + 24:320],
    }


if __name__ == "__main__":
    ids = [int(x) for x in sys.argv[1:]] or [603, 604, 605]
    out = {}
    for tid in ids:
        s = get_seal(tid)
        if s["best"] > 0:
            print(f"Seal #{tid} already solved (best {s['best']})")
            continue
        board = scramble_of(s["seed"])
        print(f"Seal #{tid} seed={s['seed']} fam={s['fam']}")
        print("  board:", board)
        moves = solve_puzzle(board)
        if moves is None:
            print("  NO SOLUTION within budget")
            continue
        # verify by replaying
        t = board[:]
        g = t.index(15)
        for m in moves:
            t[g], t[m] = t[m], t[g]
            g = m
        assert t == list(range(16)), "replay mismatch"
        print(f"  solved in {len(moves)} moves: {moves}")
        out[tid] = {"seed": s["seed"], "moves": moves}
    json.dump(out, open("/root/scripts/ka-agents/seal_moves.json", "w"), indent=1)
    print("saved", len(out), "solutions")
