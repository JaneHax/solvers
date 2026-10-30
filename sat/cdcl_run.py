"""Run one complete (CDCL) solver configuration against a DIMACS CNF.

Invoked once per GitHub Actions shard. The shard index selects both the solver
and a variant of its options, so the twenty shards explore different search
strategies rather than repeating one. A watchdog thread interrupts the solver
when the budget expires, because pysat's solve() has no native time limit.
"""

import os
import sys
import threading
import time

from pysat.formula import CNF
from pysat.solvers import Cadical195, Glucose4, MapleChrono, Minisat22

SOLVERS = [Cadical195, Glucose4, Minisat22, MapleChrono]

# Options that meaningfully change the search on random 5-SAT. Anything the
# installed pysat build rejects is dropped at runtime rather than crashing the
# shard, so an option rename cannot silently kill the whole farm.
VARIANTS = {
    "Cadical195": [
        {},
        {"elim": 0},
        {"subsume": 0},
        {"reduce": 0},
    ],
    "Glucose4": [
        {},
        {"use_phases": False},
    ],
    "Minisat22": [
        {},
        {"use_phases": False},
    ],
    "MapleChrono": [
        {},
        {"use_phases": False},
    ],
}


def build(solver_cls, opts):
    """Instantiate the solver, dropping options this build does not accept."""
    try:
        return solver_cls(**opts)
    except (TypeError, ValueError):
        return solver_cls()


def main():
    shard = int(os.environ["SHARD"])
    budget = int(os.environ["SECONDS_BUDGET"])

    solver_cls = SOLVERS[shard % len(SOLVERS)]
    variants = VARIANTS[solver_cls.__name__]
    variant = (shard // len(SOLVERS)) % len(variants)
    opts = variants[variant]

    cnf = CNF(from_file="instance.cnf")
    print(
        f"solver={solver_cls.__name__} variant={variant} opts={opts} "
        f"vars={cnf.nv} clauses={len(cnf.clauses)}",
        flush=True,
    )

    solver = build(solver_cls, opts)
    solver.append_formula(cnf.clauses)

    fired = threading.Event()

    def interrupt():
        fired.set()
        try:
            solver.interrupt()
        except Exception:  # noqa: BLE001
            pass

    watchdog = threading.Timer(budget, interrupt)
    watchdog.daemon = True
    watchdog.start()

    t0 = time.time()
    try:
        ok = solver.solve_limited(expect_interrupt=True)
    except Exception as e:  # noqa: BLE001
        print("solve raised", type(e).__name__, str(e)[:120], flush=True)
        ok = None
    elapsed = time.time() - t0
    watchdog.cancel()

    if fired.is_set():
        print(f"result=TIMEOUT elapsed={elapsed:.1f}s", flush=True)
        return 0

    print(f"result={'SAT' if ok else 'UNSAT'} elapsed={elapsed:.1f}s", flush=True)
    if not ok:
        return 0

    model = solver.get_model()
    if not model:
        print("model missing", flush=True)
        return 1

    # get_model() returns one signed literal per variable in ascending variable
    # order, so this string is directly indexable by abs(literal)-1.
    bits = "".join("1" if v > 0 else "0" for v in model)
    if len(bits) != cnf.nv:
        print(f"model length {len(bits)} != nv {cnf.nv}", flush=True)
        return 1

    path = f"model_{shard}.bits"
    with open(path, "w") as f:
        f.write(bits)
    print(f"wrote {path} len={len(bits)}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
