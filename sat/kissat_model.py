"""Convert Kissat stdout into a 0/1 assignment string.

Kissat prints:

    s SATISFIABLE
    v 1 -2 3 0

The trailing 0 is a terminator, not a variable, so it must be dropped before
building the assignment. Variables absent from the model are left as 0 (false),
which is only safe because Kissat emits a total assignment for every variable it
was given.
"""

import sys


def main():
    if len(sys.argv) != 3:
        print("usage: kissat_model.py <kissat_stdout> <nvars>", file=sys.stderr)
        return 2
    path, nvars = sys.argv[1], int(sys.argv[2])

    sat = False
    lits = []
    with open(path, "r", errors="replace") as f:
        for line in f:
            if line.startswith("s "):
                sat = "SATISFIABLE" in line and "UNSATISFIABLE" not in line
            elif line.startswith("v "):
                for tok in line[2:].split():
                    try:
                        v = int(tok)
                    except ValueError:
                        continue
                    if v != 0:
                        lits.append(v)

    if not sat:
        print("not satisfiable / no model", file=sys.stderr)
        return 1
    if not lits:
        print("empty model", file=sys.stderr)
        return 1

    assign = [0] * (nvars + 1)
    for v in lits:
        idx = abs(v)
        if 1 <= idx <= nvars:
            assign[idx] = 1 if v > 0 else 0
    if any(v == 0 for v in assign[1:]) and len(lits) < nvars:
        print(f"partial model: {len(lits)} of {nvars} variables", file=sys.stderr)

    print("".join("1" if assign[v] else "0" for v in range(1, nvars + 1)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
