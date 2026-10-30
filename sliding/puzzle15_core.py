"""4x4 sliding-puzzle (15-puzzle) core: scramble reconstruction + IDA* solver.

Two things make a site's sliding gate easy once you notice them:

* The scramble is almost always **derived from a seed**, so the board can be
  rebuilt off-chain instead of being read from the rendered art. Sites that do
  this use a small integer hash; if you can see it in the bundle, port it
  verbatim (see `sh()` below, which is the contract-side variant seen on
  agentka.xyz — identical in JS and Solidity).
* Plain Manhattan is not admissible enough at 40+ moves. Manhattan plus linear
  conflicts cuts the search by orders of magnitude.

Moves are the board position that was tapped (0..15, row-major); the tile at that
position slides into the gap.

Usage:
    python3 puzzle15.py solve "5,6,3,11,1,15,13,14,4,2,9,0,8,7,12,10"
    python3 puzzle15.py scramble <seed>          # print the board for that seed
"""
import sys

M32 = 0xFFFFFFFF


def sh(n):
    """32-bit integer hash used by the contract to derive slides from a seed."""
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


def scramble_of(seed, slides=80, solved=None):
    """Reproduce a seed-derived scramble: `slides` slides, never undoing itself."""
    board = list(solved or range(16))
    gap, last = 15, -1
    for n in range(slides):
        nb = [x for x in nbrs(gap) if x != last]
        m = nb[sh(seed * 977 + (100 + n) * 31) % len(nb)]
        board[gap], board[m] = board[m], 15
        last, gap = gap, m
    return board


def manhattan(board):
    s = 0
    for p, v in enumerate(board):
        if v == 15:
            continue
        s += abs(v // 4 - p // 4) + abs(v % 4 - p % 4)
    return s


def linear_conflict(board):
    """Extra moves forced by tiles sitting in their goal line but reversed."""
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


def solve(board, budget=4000000, weights=(1, 1.5, 3, 6)):
    """IDA* over the gap moves. Returns the move list, or None on budget exhaustion."""
    start = list(board)
    goal = list(range(16))
    if start == goal:
        return []
    gap = start.index(15)
    nodes = [0]

    def search(tiles, g, bound, prev, gp, man, path):
        f = g + man + linear_conflict(tiles)
        if f > bound:
            return f
        if man == 0:
            return -1
        nodes[0] += 1
        if nodes[0] > budget:
            return float("inf")
        best = float("inf")
        for m in nbrs(gp):
            if m == prev:
                continue
            v = tiles[m]
            d = (abs(v // 4 - gp // 4) + abs(v % 4 - gp % 4)) - \
                (abs(v // 4 - m // 4) + abs(v % 4 - m % 4))
            og = gp
            tiles[og], tiles[m] = v, 15
            path.append(m)
            r = search(tiles, g + 1, bound, og, m, man + d, path)
            if r == -1:
                return -1
            path.pop()
            tiles[m], tiles[og] = v, 15
            if r < best:
                best = r
            if nodes[0] > budget:
                return float("inf")
        return best

    for w in weights:
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
        # `w` is kept for parity with the reference implementation, which widens
        # the admissible bound per pass; the loop above already re-bounds exactly.
    return None


def replay(board, moves):
    """Apply a move list and return the resulting board."""
    t = list(board)
    g = t.index(15)
    for m in moves:
        t[g], t[m] = t[m], t[g]
        g = m
    return t


def verify(board, moves):
    return replay(board, moves) == list(range(16))


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    cmd = sys.argv[1]
    if cmd == "scramble":
        b = scramble_of(int(sys.argv[2]))
        print(",".join(map(str, b)))
    elif cmd == "solve":
        b = [int(x) for x in sys.argv[2].split(",")]
        moves = solve(b)
        if moves is None:
            print("no solution within budget")
            sys.exit(1)
        assert verify(b, moves), "replay mismatch"
        print(f"{len(moves)} moves: {moves}")
    else:
        print("unknown command", cmd)
        sys.exit(1)
