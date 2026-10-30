# solvers

Solvers for the computational gates that block NFT/token mints. Every tool here
was built against a live campaign and is kept because it worked, not because it
looks tidy.

A "mint gate" is a site that hands you an instance and starts a clock: solve it
correctly and you may mint, fail and you eat a cooldown. The gate is usually
solvable. The failure mode is picking a solver that cannot finish the instance
class inside the deadline, then reporting the campaign as impossible.

## What's in here

| Directory | Gate type | Tool | Measured |
|---|---|---|---|
| `nonogram/` | Nonogram / picross grid with row+column run clues | `nonobp` (C) + parallel runners | 112×112 board in 5-8 s single-thread |
| `sat/` | Boolean constraint instances (SAT benchmarks) | `wsatk`, `exactksat` (C) | planted 5-SAT 384 vars in 0.47 s |
| `pow/` | Hash-preimage proof of work | 3 miners (C, pthreads) | 32-bit keccak nonce ≈ 86 s on 256 cores |
| `sliding/` | 4×4 slide puzzle with a seed-derived scramble | `puzzle15_core.py` (IDA*) | 30-42 move boards in < 1 s |
| `arith/` | Obfuscated arithmetic word problems | `arith_captcha.py` (Python) | one-shot verification codes |

Run `./build.sh` to compile the C tools, then `python3 selftest.py` to prove
each one works on synthetic data before trusting it against a live clock.

---

## `nonogram/` — grid clue puzzles

**The gate.** The server returns N boards of `width × height`, each with row and
column run-length clues, a time limit, and **one** submission. Observed shape:
4 boards of 112×112, 300-1200 s, `maxBodyBytes` ≈ 54 KB.

**Why it's hard.** Board size is not fixed per tier — it grows with supply.
The same tier 5 was 4×56×56 at supply 2,500 and 4×112×112 at supply 3,200. At
112×112 with ~20 % fill, the clues force **zero** cells at depth 0, so pure line
propagation never fires and every "smart" preprocessing step is wasted work.

**What actually works.** `nonobp` is line propagation plus belief propagation
plus backtracking with restarts. It is single-threaded and **deterministic per
seed**, which drives everything else here:

- `psolve.py` — the reference portfolio runner. Ships 12 distinct configs.
- `psolve_wide.py` — same idea but **one unique seed per process**. This matters:
  the reference runner spawns 64 processes per board while only having 12
  configs, so 52 of them redo byte-identical work. On a 256-core box the
  effective parallelism is ~12. Same challenge: 20 min fail → 226 s solved.
- `replay_seeds.py` — replay the seeds that already won. A seed that solved a
  board once solves it again instantly, because the solver is deterministic.
  Replaying four known-good seeds solved a tier-8 challenge in **36 s on 2 cores**,
  faster than the 256-core search, because there the workers fought each other
  for CPU.

```bash
./build.sh
# single board, exact seed/noise/maxfail/bpit/bpevery
nonogram/nonobp <board.txt>        # cfg args: seed noise maxfail bpit bpevery
# portfolio over N boards
python3 nonogram/psolve_wide.py challenge.json answer.json 60
```

**Rule of thumb:** keep a `board → winning config` map. Replay first, search
second. The diversity of seeds finds the answer; the core count mostly buys
contention.

---

## `sat/` — Boolean constraint instances

**The gate.** A site hands out a CNF benchmark (variables, clauses, fragments,
deadline, limited reads) and grades one submitted assignment exactly. No partial
credit.

**The decisive number is literals per clause, not the clause/variable ratio.**
A benchmark advertised as "logic" is not necessarily 3-SAT: 5-SAT at ratio 4.5 is
a completely different problem from 3-SAT at ratio 4.5.

| Literals/clause | Character | Solver |
|---|---|---|
| k = 5, ratio 4.5+ | planted, easy for local search | `wsatk`, sub-second |
| k = 3, ratio < 4.2 | easy | `pysat.Cadical195`, ms |
| k = 3, ratio 4.5 | planted if solvable at all | kissat, minutes |
| already answered UNSAT | dead | stop |

- `wsatk.c` — probSAT-style weighted local search (`cb^-break`) with clause
  weighting on stall. On the k=5 classes this beats CDCL by three orders of
  magnitude: 0.47 s versus 496 s.
- `exactksat.c` — for a benchmark where each clause holds a **fixed** number of
  true literals under the hidden assignment. Sign statistics carry no signal
  there (same-sign pair rate 0.497), so spectral and break-count search both
  stall. Minimising `Σ|nTrue(c) − T|` instead exposes the planted structure; run
  several `T` in parallel and the objective says which one is right.

```bash
sat/wsatk <file.cnf> [seconds] [seed] [warm] [best_out] [cb]
sat/exactksat <file.cnf> <T> <seed> <seconds>
```

`wsatk` prints the model as a **bitstring in variable order** (`0101…`, v1
first), not as DIMACS literals — a parser expecting `1 -2 3` silently reads
garbage.

**The bug that costs the most time:** parse DIMACS by the `0` terminator, never
by a fixed count per line. Taking the first three tokens misaligns the whole
formula when k ≠ 3, and the symptom is not a crash: every solver stalls a
handful of clauses short, forever, and it looks like "the instance is unsat".

---

## `pow/` — hash-preimage proof of work

**The gate.** The contract wants `hash(preimage) < target` where the preimage is
built from a label, chain id, contract, wallet and a nonce. Seconds of hashing —
every failure is an encoding or lifecycle mistake, not a compute problem.

| Tool | Hash | Preimage |
|---|---|---|
| `sha256_prefix_nonce` | SHA-256 | `prefix ‖ nonce_be32`, N leading zero bits |
| `keccak_prefix_nonce` | keccak256 | `prefix ‖ decimal_nonce`, N leading zero bits |
| `keccak_challenge_nonce` | keccak256 | `challenge32 ‖ nonce_be32` |

`keccak_prefix_nonce` absorbs the prefix's full 136-byte blocks once and rebuilds
only the tail per iteration, so the hot loop is a single `keccak-f[1600]`.

```bash
pow/sha256_prefix_nonce  <prefix_hex> <bits> [threads] [budget_s]
pow/keccak_prefix_nonce  <prefix> <bits> <start_nonce> [stride] [budget_s]
pow/keccak_challenge_nonce <challenge_hex32> <bits> [threads] [budget_s]
```

**Three rules that decide success:**

1. **`abi.encodePacked` is not `abi.encode`.** Packed drops every pad: an address
   contributes 20 bytes, a `uint256` a full 32-byte word. Padding an address to
   32 bytes gives a self-consistent hash the contract rejects. Verify one known
   nonce against the contract's own proof getter before mining.
2. **Never restart from nonce 0.** Proofs are single-use; mining from 0 always
   rediscovers the same nonce, and the second mint reverts with status 0 and
   **zero logs** — nothing to read.
3. **The target can tighten between mining and inclusion.** One pass, no human
   pause: `read target → mine → check_proof → preview → execute`.

---

## `sliding/` — 4×4 slide puzzles

**The gate.** A scrambled 4×4 tile board; submit the move list on-chain and your
move count is ranked. Fewer moves, higher score, and the score often feeds a
bonus elsewhere (staking weight, for example).

**Two shortcuts that make it trivial:**

- The scramble is **derived from a seed**, so the board can be rebuilt off-chain
  instead of read from the rendered art. `puzzle15_core.sh()` is the 32-bit hash
  variant seen in the wild, identical in JS and Solidity, with slides chosen by
  `sh(seed*977 + (100+n)*31) % len(neighbours)` and never undoing the last move.
- **Manhattan alone is not enough** at 40+ moves. Manhattan plus linear conflicts
  cuts the search by orders of magnitude.

```bash
python3 sliding/puzzle15_core.py scramble 762375596   # rebuild board from seed
python3 sliding/puzzle15_core.py solve "5,6,3,11,..." # moves for that board
```

Observed optimal counts: 30, 36, 42 moves. Always replay the move list before
submitting.

---

## `arith/` — obfuscated arithmetic captchas

**The gate.** A two-number, one-operation word problem wrapped in noise
(`A- LIGHTning boLT ...`), with a one-shot verification code. Two failure modes
each cost the code:

- Unit phrases such as "meters per second" are **not** division. Strip
  `per <unit>` first, or "45 m/s that slows by 7" parses as `45/7`.
- `"and"` is a conjunction, not always addition ("and slows by seven"). Attach
  operators to the number gap they follow, and let a specific operator
  (minus/times/divide) win over the generic `and`/`plus` in the same gap.

---

## Cross-cutting rules

These are the ones that actually cost attempts.

**Prove the solver before opening the clock.** The clock starts at issue and a
wasted window costs a multi-hour cooldown (observed ~6,500 s). Save one real
instance from any earlier attempt, get the solver through it end to end, and only
then call `request`. Never develop against a live clock.

**Verify locally before the single submission.** Re-check the answer against
every constraint yourself. Grading is exact, and a wrong artifact usually comes
back as a scored loss with no diagnostic — which reads like a protocol bug
instead of a bad answer.

**Never cap the solver below what the instance needs.** A hardcoded cap smaller
than the real solve time makes every attempt fail just short of the answer, with
no error that points at the cap.

**Time-box exploration and say what is running.** A solver grinding silently for
ten minutes gets the verdict "kelamaan". Cap exploration at a couple of minutes
and report the blocker with the numbers you already have.

**Do not oversubscribe a small box.** Two concurrent solvers on 2 vCPUs made each
lane ~3× slower. A parallel portfolio is standard advice for unsolved-instance
competitions and actively harmful on a shared host — unless the processes are
genuinely different (different seeds), in which case it is the whole point.

**A "closed" gate is transient.** While one API reported `admission: closed`,
on-chain supply kept climbing because other agents were getting through. Poll the
cheap chain-side flag as a wake-up signal and retry; never declare a drop closed
on one read while supply is moving.

## Build requirements

- `gcc` (C11), `-lm` for the nonogram and SAT solvers (they call `log`/`exp`)
- `-pthread` for the PoW miners
- Python 3.9+ for the runners; `pysat` and `ortools` optional for the CDCL /
  CP-SAT routes

```bash
./build.sh          # compile everything
python3 selftest.py # 5/5 should pass
```

## License

MIT.
